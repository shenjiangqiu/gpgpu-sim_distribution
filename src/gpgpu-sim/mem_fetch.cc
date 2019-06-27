// Copyright (c) 2009-2011, Tor M. Aamodt
// The University of British Columbia
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// Redistributions in binary form must reproduce the above copyright notice, this
// list of conditions and the following disclaimer in the documentation and/or
// other materials provided with the distribution.
// Neither the name of The University of British Columbia nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//#define TLBDEBUG
#include"debug_macro.h"


#include "mem_fetch.h"
#include "mem_latency_stat.h"
#include "shader.h"
#include "visualizer.h"
#include "gpu-sim.h"
#include "page_manager.hpp"
unsigned mem_fetch::sm_next_mf_request_uid=1;
#ifdef TLBDEBUG
int mem_fetch::m_nums=0;
#endif
void mem_fetch::reset_raw_addr(){

       m_mem_config->m_address_mapping.addrdec_tlx(physic_addr, &m_raw_addr);

}
//std::unordered_map<mem_fetch*,unsigned long long> mf_map;
mem_fetch::mem_fetch( const mem_access_t &access, 
                      const warp_inst_t *inst,
                      unsigned ctrl_size, 
                      unsigned wid,
                      unsigned sid, 
                      unsigned tpc, 
                      const struct memory_config *config,
					  mem_fetch *m_original_mf,
					  mem_fetch *m_original_wr_mf,
                      mem_fetch* pw_origin):/* is_in_response_queue(false),magic_number(0x12341234),*/finished_tlb(false),pw_origin(pw_origin)

{
    #ifdef TLBDEBUG
    m_nums++;
    #endif
   // printdbg_tlb("mem_fetch(),mf total nums: %d,current id %u\n",m_nums,sm_next_mf_request_uid);
    

   m_request_uid = sm_next_mf_request_uid++;
   m_access = access;
   if (inst)
   {
       m_inst = *inst;
       assert(wid == m_inst.warp_id());
       if (inst->space.get_type() != global_space)
       { //only global space requset need tlb,currently/TODO
           finished_tlb = true;
       }
   }
   m_data_size = access.get_size();
   m_ctrl_size = ctrl_size;
   m_sid = sid;
   m_tpc = tpc;
   m_wid = wid;

   virtual_addr=access.get_addr();
   if (sid == -1)
       physic_addr = virtual_addr; //that is a wb request, no need to do the translation
   else
       physic_addr = global_page_manager->translate(virtual_addr);

   config->m_address_mapping.addrdec_tlx(physic_addr,&m_raw_addr);
   m_partition_addr = config->m_address_mapping.partition_address(physic_addr);

   m_type = m_access.is_write()?WRITE_REQUEST:READ_REQUEST;
   m_timestamp = gpu_sim_cycle + gpu_tot_sim_cycle;
   m_timestamp2 = 0;
   m_status = MEM_FETCH_INITIALIZED;
   m_status_change = gpu_sim_cycle + gpu_tot_sim_cycle;
   m_mem_config = config;
   icnt_flit_size = config->icnt_flit_size;
   original_mf = m_original_mf;
   original_wr_mf = m_original_wr_mf;
   //mf_map[this]=this->get_addr();
   #ifdef TLBDEBUG
   auto addr=this->get_physic_addr();
   #endif   
   //printdbg_tlb("mf: %p,addr:%llX\n",this,addr);
}

mem_fetch::~mem_fetch()
{

    /* if(is_in_response_queue){
        throw;
    } */
    m_status = MEM_FETCH_DELETED;
    #ifdef TLBDEBUG
    m_nums--;
    #endif
   // printdbg_tlb("~mem_fetch: mf:%p ,nums:%d\n",this,m_nums);
}

#define MF_TUP_BEGIN(X) static const char* Status_str[] = {
#define MF_TUP(X) #X
#define MF_TUP_END(X) };
#include "mem_fetch_status.tup"
#undef MF_TUP_BEGIN
#undef MF_TUP
#undef MF_TUP_END

void mem_fetch::print( FILE *fp, bool print_inst ) const
{
    if( this == NULL ) {
        fprintf(fp," <NULL mem_fetch pointer>\n");
        return;
    }
    fprintf(fp,"  mf: uid=%6u, sid%02u:w%02u, part=%u, ", m_request_uid, m_sid, m_wid, m_raw_addr.chip );
    m_access.print(fp);
    if( (unsigned)m_status < NUM_MEM_REQ_STAT ) 
       fprintf(fp," status = %s (%llu), ", Status_str[m_status], m_status_change );
    else
       fprintf(fp," status = %u??? (%llu), ", m_status, m_status_change );
    if( !m_inst.empty() && print_inst ) m_inst.print(fp);
    else fprintf(fp,"\n");
}

void mem_fetch::set_status( enum mem_fetch_status status, unsigned long long cycle ) 
{
    m_status = status;
    m_status_change = cycle;
}

bool mem_fetch::isatomic() const
{
   if( m_inst.empty() ) return false;
   return m_inst.isatomic();
}

void mem_fetch::do_atomic()
{
    m_inst.do_atomic( m_access.get_warp_mask() );
}

bool mem_fetch::istexture() const
{
    if( m_inst.empty() ) return false;
    return m_inst.space.get_type() == tex_space;
}

bool mem_fetch::isconst() const
{ 
    if( m_inst.empty() ) return false;
    return (m_inst.space.get_type() == const_space) || (m_inst.space.get_type() == param_space_kernel);
}

/// Returns number of flits traversing interconnect. simt_to_mem specifies the direction
unsigned mem_fetch::get_num_flits(bool simt_to_mem){
	unsigned sz=0;
	// If atomic, write going to memory, or read coming back from memory, size = ctrl + data. Else, only ctrl
	if( isatomic() || (simt_to_mem && get_is_write()) || !(simt_to_mem || get_is_write()) )
		sz = size();
	else
		sz = get_ctrl_size();

	return (sz/icnt_flit_size) + ( (sz % icnt_flit_size)? 1:0);
}



