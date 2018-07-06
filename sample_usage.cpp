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

int main(int argc, char** argv) {

    //Instantiate new handle, AFU UUID is passed as parameter
    opaeutils::AFU_Handle afu_handle("092a3e62-81c5-499a-ae2c-62ff4788fadd");

    //Declare desired buffers (the order in which they are declared will also be their order in the workspace)
    afu_handle.addBuffer("control", 1);
    afu_handle.addBuffer("edges", num_edges, sizeof(edge_t));
    afu_handle.addBuffer("verts", num_verts, sizeof(uint32_t));
    afu_handle.addBuffer("updates", num_verts, sizeof(update_t));
    afu_handle.addAlignedBuffer("al_verts", num_verts, sizeof(vert_t));

    //Get information about workspace before allocating it
    cout << "Workspace CLs: " << afu_handle.getWorkspaceCLs() << endl;
    cout << "Workspace Bytes: " << afu_handle.getWorkspaceBytes() << endl;

    cout << "Vertex Buffer CLs: " << afu_handle.getBufferCLs("verts") << endl;
    cout << "Vertex Buffer Bytes: " << afu_handle.getBufferBytes("verts") << endl;


    //Allocate workspace, new buffers cannot be decalered until the workspace is freed
    afu_handle.allocateWorkspace();

    //Get pointers to the start of each buffer
    uint64_t* afu_control = afu_handle.getBufferPtr("control");
    uint64_t* afu_edges = afu_handle.getBufferPtr("edges");
    uint64_t* afu_verts = afu_handle.getBufferPtr("verts");
    uint64_t* afu_updates = afu_handle.getBufferPtr("updates");
    uint64_t* afu_al_verts = afu_handle.getBufferPtr("al_verts");

    cout << "Worspace start ADDR: " << std::hex << afu_control << std::dec << endl;
    cout << "Edge buffer start ADDR: " << std::hex << afu_edges << std::dec << endl;
    cout << "Vert buffer start ADDR: " << std::hex << afu_verts << std::dec << endl;
    cout << "Updt buffer start ADDR: " << std::hex << afu_updates << std::dec << endl;

    //Populate vertex buffer [ (31 bit level, 1 bit active) ]
    //This is done without the use of the helper iterator
    uint32_t *itt_ptr;
    
    itt_ptr = (uint32_t*)afu_verts;
    for (int i = 0; i < num_verts; i++) {
        *itt_ptr = verts[i].level << 1;
        if (verts[i].active) {
            *itt_ptr = 1 | *itt_ptr;
        }

        itt_ptr += 1;
    }

    //Populate edge buffer [ (32 bit src, 32 bit dst) ]
    //Using the CL iterator, works perfectly fine with well-aligned buffers
    opaeutils::CLIterator<edge_t> edge_itt(afu_edges, sizeof(edge_t));
    edge_t* edge_ptr = edge_itt.start();
    for (int i = 0; i < num_edges; i++) {
        *edge_ptr = edges[i];
        edge_ptr = edge_itt.next();
    }

    //Populating misaligned vertex buffer [ (32 bit level, 8 bit active) ]
    //Using the CL iterator to simplify the population of a misaligned buffer
    opaeutils::CLIterator<vert_t> al_vert_itt(afu_al_verts, sizeof(vert_t));
    vert_t* vert_ptr = al_vert_itt.start();
    for (int i = 0; i < num_verts; i++) {
        *vert_ptr = verts[i];
        vert_ptr = al_vert_itt.next();
    }

    //Free workspace
    afu_handle.freeBuffer();


    //CSR Wrappers:
    afu_handle.writeCSR(2, num_verts);
    uint64_t csr_2 = afu_handle.readCSR(2);

    return (0);
}
