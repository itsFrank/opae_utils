#include <iostream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <sys/time.h>
#include <string>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <cstdlib> 
#include <vector>
#include <ctime> 
#include <fstream>
#include <bitset>

#include "opae_svc_wrapper.h"
#include "csr_mgr.h"

#include "simple_cli.h"
#include "opae_utils.h"


#ifdef DEBUG
#undef DEBUG
#endif
#define DEBUG(msg) std::cout << __FILE__ << " (" << __LINE__ << "): " << msg << std::endl;

#define BYTES_PER_CL 64


using namespace std;

typedef struct {
    uint32_t src;
    uint32_t dst;
} edge_t;

typedef struct __attribute__ ((packed)) { //without packing, bool is 4 bytes
    uint32_t level;
    bool active;
} vert_t;

typedef struct {
    uint32_t dst;
    uint32_t level;
} update_t;

int partition_src(vector<edge_t> &edges, int low, int high);
int partition_dst(vector<edge_t> &edges, int low, int high);
void quicksort_src_h(vector<edge_t> &edges, int low, int high);
void quicksort_dst_h(vector<edge_t> &edges, int low, int high);
void quicksort_src(vector<edge_t> &edges);
void quicksort_dst(vector<edge_t> &edges);


int main(int argc, char** argv) {
    //------ CLI -------
    scli::CLI options;

    options.addPositional("graphfile", "");
    options.addPositional("startv", "");
    options.addOption("printdelay", "-pd", "when set, the value of the CSRs will be printed every X ms (x is the number following the flag)");
    options.addFlag("afu", "-afu", "");
    options.addFlag("cpu", "-cpu", "");

    options.parse(argc, argv);

    string graph_file = options["graphfile"];
    int start_vert = options.asInt("startv");
    bool print_en = options.is("printdelay");
    bool afu_en = options.is("afu");
    bool cpu_en = options.is("cpu");
    long csr_readout_ms_delay = print_en ? options.asInt("printdelay") : 0;

    if (options.error()) {
        cout << "An error occured when parsing input" << endl;
        return -1;
    }

    long tv_sec = csr_readout_ms_delay / 1000; 
    long tv_nsec = (csr_readout_ms_delay - ((csr_readout_ms_delay / 1000) * 1000)) * 1000000; 

    //------ END CLI ------

    //------ GRAPH READ ------

    int num_verts;
    int num_edges;
    int tmp_int;
    vector<edge_t> edges;
    vector<vert_t> verts;

    ifstream file(graph_file.c_str());

    file >> num_verts;
    file >> num_edges;

    for (int i = 0; i < num_verts; i++) {
        vert_t new_vert;
        new_vert.level = i ;//0xFFFFFFFF;
        new_vert.active = false;

        verts.push_back(new_vert);
    }

    for (int i = 0; i < num_edges; i++) {
        edge_t new_edge;
        
        file >> tmp_int;
        new_edge.src = tmp_int;

        file >> tmp_int;
        new_edge.dst = tmp_int;

        edges.push_back(new_edge);
    }

    //------ END GRAPH READ ------

    //------ PREPROCESSING ------
    
    //Sort by src
    quicksort_src(edges);
    
    //Remove duplicates
    int sect_start = 0;
    int current_src = edges[0].src;

    for (int i = 0; i < edges.size(); i++) {
        if (edges[i].src != current_src) {

            quicksort_dst_h(edges, sect_start, i - 1);

            sect_start = i;
            current_src = edges[i].src;
        }
    }

    current_src = -1;
    int current_dst = -1;

    vector<edge_t> reduced_edges;

    for (int i = 0; i < edges.size(); i++) {
        if (edges[i].src == current_src && edges[i].dst == current_dst) {
            
        } else {
            reduced_edges.push_back(edges[i]);
        }

        if (edges[i].src != current_src) current_src = edges[i].src;
        if (edges[i].dst != current_dst) current_dst = edges[i].dst;
    }

    //------ END PREPROCESSING ------

    // for (int i = 0; i < edges.size(); i++) {
    //     cout << "E{" << i << "}: " << edges[i].src << "->" << edges[i].dst;
    //     if (i < reduced_edges.size()) {
    //         cout << " ||| " << reduced_edges[i].src << "->" << reduced_edges[i].dst << endl;
    //     } else {
    //         cout << endl;
    //     }
    // }

    edges = reduced_edges;
    num_edges = edges.size();

    verts[start_vert].level = 0;
    verts[start_vert].active = true;

    // if (cpu_en) {
    //     bool updates_written = false;

    //     do {
    //         updates_written = false;

    //         vector<update_t> updates;

    //         for (int i = 0; i < num_edges; i++) {
    //             edge_t edge = edges[i];
    //             vert_t src_v = verts[edge.src];
    //             if (src_v.active) {
    //                 update_t new_update;
    //                 new_update.dst = edge.dst;
    //                 new_update.level = src_v.level + 1;

    //                 updates.push_back(new_update);
    //             }
    //         }   

    //         for (int i = 0; i < num_verts; i++) {
    //             verts[i].active = false;
    //         }         

    //         cout << updates.size() << endl;

    //         for (int i = 0; i < updates.size(); i++) {
    //             update_t update = updates[i];

    //             if (update.level < verts[updates[i].dst].level) {
    //                 verts[updates[i].dst].level = update.level;
    //                 verts[updates[i].dst].active = true;

    //                 updates_written = true;
    //             }
    //         }

    //     } while (updates_written);

    //     for (int i = 0; i < num_verts; i++) {
    //         cout << "V[" << i << "]: " << verts[i].level << endl;
    //     }
    // }

    if (afu_en) {
        opaeutils::AFU_Handle afu_handle("092a3e62-81c5-499a-ae2c-62ff4788fadd");

        afu_handle.addBuffer("control", 1);
        afu_handle.addBuffer("edges", num_edges, sizeof(edge_t));
        afu_handle.addBuffer("verts", num_verts, sizeof(uint32_t));
        afu_handle.addBuffer("updates", num_verts, sizeof(update_t));
        afu_handle.addAlignedBuffer("al_verts", num_verts, sizeof(vert_t));


        cout << "Workspace CLs: " << afu_handle.getWorkspaceCLs() << endl;
        cout << "Workspace Bytes: " << afu_handle.getWorkspaceBytes() << endl;

        afu_handle.allocateWorkspace();

        uint64_t* afu_control = afu_handle.getBufferPtr("control");
        uint64_t* afu_edges = afu_handle.getBufferPtr("edges");
        uint64_t* afu_verts = afu_handle.getBufferPtr("verts");
        uint64_t* afu_updates = afu_handle.getBufferPtr("updates");
        uint64_t* afu_al_verts = afu_handle.getBufferPtr("al_verts");

        cout << "Worspace start ADDR: " << std::hex << afu_control << std::dec << endl;
        cout << "Edge buffer start ADDR: " << std::hex << afu_edges << std::dec << endl;
        cout << "Vert buffer start ADDR: " << std::hex << afu_verts << std::dec << endl;
        cout << "Updt buffer start ADDR: " << std::hex << afu_updates << std::dec << endl;

        uint64_t afu_num_verts = num_verts;
        uint64_t afu_num_edges = num_edges;

        //Populate vertex buffer [ (31 bit level, 1 bit active) ]
        uint32_t *itt_ptr;
        
        itt_ptr = (uint32_t*)afu_verts;
        for (int i = 0; i < num_verts; i++) {
            *itt_ptr = verts[i].level << 1;
            if (verts[i].active) {
                *itt_ptr = 1 | *itt_ptr;
            }

            itt_ptr += 1;
        }

        opaeutils::CLIterator<edge_t> edge_itt(afu_edges, sizeof(edge_t));
        edge_t* edge_ptr = edge_itt.start();
        for (int i = 0; i < num_edges; i++) {
            *edge_ptr = edges[i];
            edge_ptr = edge_itt.next();
        }

        //Populating misaligned vertex buffer
        opaeutils::CLIterator<vert_t> al_vert_itt(afu_al_verts, sizeof(vert_t));
        vert_t* vert_ptr = al_vert_itt.start();
        for (int i = 0; i < num_verts; i++) {
            *vert_ptr = verts[i];
            vert_ptr = al_vert_itt.next();
        }

        //Print for sanity

            cout << "Num verts: " << num_verts << endl;
            cout << "Num edges: " << num_edges << endl;
            vert_ptr = al_vert_itt.start();
            itt_ptr = (uint32_t*)afu_verts;
            cout << "Vertex Data:\n";
            for (int i = 0; i < num_verts; i++) {
                cout << bitset<32>(*itt_ptr) << "\t\t" << bitset<32>(verts[i].level) << "\t\t" << bitset<32>((*vert_ptr).level) << ":" << bitset<8>((*vert_ptr).active) <<  endl;
                itt_ptr += 1;
                vert_ptr = al_vert_itt.next();
            }

            itt_ptr = (uint32_t*)afu_edges;
            cout << "\nEdge Data:\n";
            for (int i = 0; i < num_edges; i++) {
                cout << *itt_ptr;
                itt_ptr += 1;
                cout << " : " << *itt_ptr << std::dec;
                cout << "\t\t" << edges[i].src << " : " << edges[i].dst << endl;
                itt_ptr += 1;
            }

            char* al_vert_cl_check_ptr = (char*)afu_al_verts;

            for (int i = 0; i < afu_handle.getBufferCLs("al_verts"); i++) {
                for (int j = 0; j < 12; j++) {
                    cout << *((uint32_t*)al_vert_cl_check_ptr);
                    al_vert_cl_check_ptr += 4;
                    cout << ":" << bitset<8>(*al_vert_cl_check_ptr) << endl;
                    al_vert_cl_check_ptr += 1;
                }

                for (int j = 0; j < 4; j++) {
                    cout << bitset<8>(*al_vert_cl_check_ptr) << " ";
                    al_vert_cl_check_ptr += 1;
                }
                cout << "\n";
            }

        // // Write the parameters to the AFU
        // afu_handle.writeCSR(1, (uint64_t) afu_control); //Control Word
        
        // // The pause time waiting for the AFU to be done
        // struct timespec pause;
        // pause.tv_sec = 1;
        // pause.tv_nsec = 0;
        // nanosleep(&pause, NULL);

        // //Timers
        // opaeutils::Timer afu_timer;
        // opaeutils::Timer print_timer;
        // afu_timer.start();
        // print_timer.start();

        // // Signal the AFU to startup
        // afu_handle.writeCSR(0, 1);
        // // *afu_control = 0;
        // DEBUG("Started AFU");
        
        // *afu_control = 1;

        // pause.tv_sec = 0;
        // pause.tv_nsec = 1;

        // do { 
        //     if (print_en && print_timer.elapsed_ms() > csr_readout_ms_delay) {
        //         print_timer.start();
        //         cout << "Rd CSR 1: " << afu_handle.readCSR(1) << std::dec << endl;
        //         cout << "Rd CSR 7 [15:0]: " << afu_handle.readCSR(7, 15, 0) << std::dec << endl;
        //         cout << "AFU Control address: \t" << *afu_control << std::dec << endl;
        //         cout << endl;
        //     }

        //     if (*afu_control == 3) {
        //         //cout << "Telling afu to start new batch" << endl;
        //         *afu_control = 1;
        //     }

        //     nanosleep(&pause, NULL);
        // } while (*afu_control != 4);

        // DEBUG("AFU done...");

        // printf("Elapsed time: %ld milliseconds\n", afu_timer.elapsed_ms());

        // pause.tv_sec = 1;
        // pause.tv_nsec = 0;
        // nanosleep(&pause, NULL);

        // cout << "Rd CSR 1: " << afu_handle.readCSR(1) << std::dec << endl;
        // cout << "Rd CSR 7 [63:32]: " << afu_handle.readCSR(7, 63, 32) << std::dec << endl;
        // cout << "AFU Control address: \t" << *afu_control << std::dec << endl;
        // cout << endl;

        // afu_handle.freeBuffer();
    }


    return (0);
}

int partition_src(vector<edge_t> &edges, int low, int high) {
    edge_t pivot = edges[high];    // pivot
    int i = low - 1;  // Index of smaller element
 
    for (int j = low; j <= high - 1; j++)
    {
        // If current element is smaller than or
        // equal to pivot
        if (edges[j].src <= pivot.src)
        {
            i++;    // increment index of smaller element
            edge_t tmp = edges[i];
            edges[i] = edges[j];
            edges[j] = tmp;
        }
    }
    edge_t tmp = edges[i + 1];
    edges[i + 1] = edges[high];
    edges[high] = tmp;
    return i + 1;
}

int partition_dst(vector<edge_t> &edges, int low, int high) {
    edge_t pivot = edges[high];    // pivot
    int i = low - 1;  // Index of smaller element
 
    for (int j = low; j <= high - 1; j++)
    {
        // If current element is smaller than or
        // equal to pivot
        if (edges[j].dst <= pivot.dst)
        {
            i++;    // increment index of smaller element
            edge_t tmp = edges[i];
            edges[i] = edges[j];
            edges[j] = tmp;
        }
    }
    edge_t tmp = edges[i + 1];
    edges[i + 1] = edges[high];
    edges[high] = tmp;
    return i + 1;
}

void quicksort_src_h(vector<edge_t> &edges, int low, int high) {
    if (low < high) {
        int pivot = partition_src(edges, low, high);

        quicksort_src_h(edges, low, pivot - 1);
        quicksort_src_h(edges, pivot + 1, high);
    }
}

void quicksort_dst_h(vector<edge_t> &edges, int low, int high) {
    if (low < high) {
        int pivot = partition_dst(edges, low, high);

        quicksort_dst_h(edges, low, pivot - 1);
        quicksort_dst_h(edges, pivot + 1, high);
    }
}

void quicksort_src(vector<edge_t> &edges) {
    quicksort_src_h(edges, 0, edges.size() - 1);
}

void quicksort_dst(vector<edge_t> &edges) {
    quicksort_dst_h(edges, 0, edges.size() - 1);
}

