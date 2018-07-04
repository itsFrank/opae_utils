/*
    File: opae_utils.h                                                                                                                              
    Author: Francis O'Brien                                                                                                                           
    Email: francis.obrien@mail.utoronto.ca                                                                                                            
    Create Date: 07-04-2018                                                                                                                             
    -----------------------------------------                                                                                                         
    Description                                                                                                                                              
        A set of functions and objects to simplify CPU-side workspace and buffer management for Intel HARP OPAE AFUs
    -----------------------------------------
    History                                                                                                                                           
        07-04-2018 :    created  
*/


#pragma once
#ifndef opaeutils
#define opaeutils


#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <sys/time.h>

#include "opae_svc_wrapper.h"
#include "csr_mgr.h"

#define BYTES_PER_CL 64
#define INT64_PER_CL 8

#define DEBUG(msg) std::cout << __FILE__ << " (" << __LINE__ << "): " << msg << std::endl;

namespace opaeutils {
    using namespace std;

    

    struct buffer_t {
        uint64_t num_cls;
        uint64_t cl_offset;
    };

    class AFU_Handle {
        private:
            string UUID;
            OPAE_SVC_WRAPPER* fpgaHandle;
            CSR_MGR* csrs;

            bool workspace_allocated;

            uint64_t* p_workspace;

            uint64_t cl_offset;

            map<string, buffer_t> buffers;
            vector<string> buffer_order;

        public:

            static buffer_t MakeBuffer(uint64_t num_cls, uint64_t cl_offset) {
                buffer_t buffer;
                buffer.num_cls = num_cls;
                buffer.cl_offset = cl_offset;

                return buffer;
            }

            AFU_Handle(string UUID) {
                this->cl_offset = 0;
                this->workspace_allocated = false;

                this->UUID = UUID;
            }

            uint64_t getWorkspaceCLs() {
                return this->cl_offset;
            }

            uint64_t getWorkspaceBytes() {
                return this->cl_offset * BYTES_PER_CL;
            }

            uint64_t getBufferCLs(string name) {
                return this->buffers[name].num_cls;
            }

            uint64_t getBufferBytes(string name) {
                return this->buffers[name].num_cls * BYTES_PER_CL;
            }


            bool allocateWorkspace() {
                if (workspace_allocated) {
                    throw std::runtime_error("[OPAE-UTILS] ERROR: Cannot re-allocate woprspace");
                }

                this->fpgaHandle = new OPAE_SVC_WRAPPER(this->UUID.c_str());

                assert(this->fpgaHandle);
                assert(this->fpgaHandle->isOk());

                this->csrs = new CSR_MGR(*(this->fpgaHandle));
                assert(this->csrs);

                this->p_workspace = (uint64_t*)this->fpgaHandle->allocBuffer(this->cl_offset * BYTES_PER_CL);
                assert(NULL != p_workspace);
                
                this->workspace_allocated = true;

                return true;
            }

            void addBuffer(string name, uint64_t numCL) {
                if (workspace_allocated) {
                    cerr << "[OPAE-UTILS] ERROR: Cannot add new buffer after allocating workspace" << endl;
                    exit(1);
                }

                this->buffer_order.push_back(name);
                this->buffers[name] = MakeBuffer(numCL, this->cl_offset);
                this->cl_offset += numCL;
            }

            void addBuffer(string name, int num_elements, size_t size_elements) {
                if (workspace_allocated) {
                    cerr << "[OPAE-UTILS] ERROR: Cannot add new buffer after allocating workspace" << endl;
                    exit(1);
                }
                
                uint64_t bytes = (uint64_t)(num_elements * size_elements);
                uint64_t numCL = (bytes / BYTES_PER_CL) + (bytes % BYTES_PER_CL == 0 ? 0 : 1);
                
                this->buffer_order.push_back(name);
                this->buffers[name] = MakeBuffer(numCL, this->cl_offset);
                this->cl_offset += numCL;
            }

            void addAlignedBuffer(string name, int num_elements, size_t size_elements) {
                if (workspace_allocated) {
                    cerr << "[OPAE-UTILS] ERROR: Cannot add new buffer after allocating workspace" << endl;
                    exit(1);
                }
                
                uint64_t num_elem_per_cl = BYTES_PER_CL / size_elements;
                uint64_t numCL = (num_elements / num_elem_per_cl) + (num_elements % num_elem_per_cl == 0 ? 0 : 1);

                this->buffer_order.push_back(name);
                this->buffers[name] = MakeBuffer(numCL, this->cl_offset);
                this->cl_offset += numCL;
            }

            uint64_t* getWorkspacePtr(){
                if (!workspace_allocated) {
                    cerr << "[OPAE-UTILS] ERROR: Allocate workspace before requesting pointers" << endl;
                    exit(1);
                }
                
                return this->p_workspace;
            }

            uint64_t* getBufferPtr(string name) {
                 if (!workspace_allocated) {
                    cerr << "[OPAE-UTILS] ERROR: Allocate workspace before requesting pointers" << endl;
                    exit(1);
                }
                
                uint64_t* ptr = this->p_workspace;
                return ptr + this->buffers[name].cl_offset;
            }

            void writeCSR(int csr_id, uint64_t data){
                this->csrs->writeCSR(csr_id, data);
            }

            uint64_t readCSR(int csr_id){
                return this->csrs->readCSR(csr_id);
            }

            uint64_t readCSR(int csr_id, int high_bit, int low_bit){
                uint64_t csr = this->csrs->readCSR(csr_id);
                uint64_t mask = 0xFFFFFFFFFFFFFFFF >> (63 - high_bit);
                
                csr = csr & mask;
                csr = csr >> low_bit;

                return csr;
            }

            void freeBuffer(){
                this->fpgaHandle->freeBuffer((void*)this->p_workspace);
                this->workspace_allocated = false;
            }

            ~AFU_Handle(){
                if (workspace_allocated) this->fpgaHandle->freeBuffer((void*)this->p_workspace);
            }
    };

    template <typename T>
    class CLAlignedBufferItterator {
        private:
            const size_t elem_byte_size;
            const size_t misaligned_bytes;
            const int elem_per_cl;
            int cl_elem_offset;

            const char* base_ptr;
            char* current_ptr;

        public:
            CLAlignedBufferItterator(size_t elem_byte_size, char* base_ptr) {
                this->elem_byte_size = elem_byte_size;
                this->elem_per_cl = BYTES_PER_CL / elem_byte_size;
                this->misaligned_bytes = BYTES_PER_CL - (elem_byte_size * this->elem_per_cl);
                this->base_ptr = base_ptr;
            }

            T* start() {
                this->cl_elem_offset = 1;
                this->current_ptr = this->base_ptr;
                return static_cast<T*>(this->current_ptr);
            }

            T* next() {
                if (this->cl_elem_offset == this->elem_per_cl) {
                    this->current_ptr += this->misaligned_bytes;
                    this->cl_elem_offset = 1;
                } else {
                    this->current_ptr += this->elem_byte_size;
                    this->cl_elem_offset += 1;
                }

                return static_cast<T*>(this->current_ptr);
            }
    };

    class Timer {
        private:
            struct timeval start_time;

        public:
            Timer() {}

            void start() {
                gettimeofday(&(this->start_time), NULL);
            }

            long elapsed_ms() {
                struct timeval end;
                long mtime, seconds, useconds;

                gettimeofday(&end, NULL);
                seconds  = end.tv_sec  - this->start_time.tv_sec;
                useconds = end.tv_usec - this->start_time.tv_usec;
                mtime = ((seconds) * 1000 + useconds/1000.0) + 0.5;
                return mtime;
            }

            long elapsed_us() {
                struct timeval end;
                long utime, seconds, useconds;

                gettimeofday(&end, NULL);
                seconds  = end.tv_sec  - this->start_time.tv_sec;
                useconds = end.tv_usec - this->start_time.tv_usec;
                utime = ((seconds * 1000000) + useconds);
                return utime;
            }

            double elapsed_s() {
                struct timeval end;
                long seconds, useconds;
                double stime;

                gettimeofday(&end, NULL);
                seconds  = end.tv_sec  - this->start_time.tv_sec;
                useconds = end.tv_usec - this->start_time.tv_usec;
                stime = seconds + (((double)useconds) / 1000000.0);
                return stime;
            }
    };
}


#endif