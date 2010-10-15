#ifndef HEMELB_NET_H
#define HEMELB_NET_H

#include "constants.h"
#include "mpiInclude.h"
#include "D3Q15.h"

// Superficial site data
struct WallData
{
    // estimated boundary normal (if the site is an inlet/outlet site)
    double boundary_nor[3];
    // estimated minimum distance (in lattice units) from the
    // inlet/outlet boundary;
    double boundary_dist;
    // estimated wall normal (if the site is close to the wall);
    double wall_nor[3];
    // estimated minimum distance (in lattice units) from the wall;
    // if the site is close to the wall surface
    double wall_dist;
    // cut distances along the 14 non-zero lattice vectors;
    // each one is between 0 and 1 if the surface cuts the corresponding
    // vector or is equal to 1e+30 otherWallDatawise
    double cut_dist[D3Q15::NUMVECTORS - 1];

};

// WallBlock is a member of the structure Net and is employed to store the data
// regarding the wall, inlet and outlet sites.
struct WallBlock
{
    WallData *wall_data;
};

// ProcBlock has one member called proc_id. Later on in 
// this file, an array called proc_block will be defined, which is a
// member of the structure Net (allocated in main.cc).  For each global block,
// *proc_id is an array containing the ranks on which individual
// lattice sites reside.
struct ProcBlock
{
    int *proc_id;
};

// DataBlock has one member called site_data. Block means macrocell of fluid sites (voxels).
// Later on in this file, two arrays, map_block[] and data_block[], will be defined,
// which are members of the structure Net (allocated in main.cc).  These arrays contain
// *site_data of global blocks. site_data[] is an array containing individual lattice site data
// within a global block.
struct DataBlock
{
    unsigned int *site_data;
};

// NeighProc is part of the Net (defined later in this file).  This object is an element of an array
// (called neigh_proc[]) and comprises information about the neighbouring processes to this process.  
struct NeighProc
{
    int id; // Rank of the neighbouring processor.
    int fs; // Number of distributions shared with neighbouring
    // processors.

    short int *f_data; // Coordinates of a fluid site that streams to the on
    // neighbouring processor "id" and
    // streaming direction

    int f_head;
    int *f_recv_iv;

    // buffers needed for convergence-enabled simulations
    double *f_to_send;
    double *f_to_recv;

    int *f_send_id;
};

class Net
{
  public:
    Net(int &iArgumentCount, char* iArgumentList[]);
    ~Net();

    void Abort();
    bool IsCurrentProcRank(int iRank);
    bool IsCurrentProcTheIOProc();
    char *GetCurrentProcIdentifier();

    // declarations of all the functions used
    int *netProcIdPointer(int site_i, int site_j, int site_k);
    int netFindTopology(int *depths);
    void netInit(int totalFluidSites);

    double GetCutDistance(int iSiteIndex, int iDirection);
    double* GetNormalToWall(int iSiteIndex);

    int procs; // Number of processors.
    int neigh_procs; // Number of neighbouring rocessors.
    int err;
    int my_inter_sites, my_inner_sites; // Site on this process that do and do not need
    // information from neighbouring processors.
    int my_inner_collisions[COLLISION_TYPES]; // Number of collisions that only use data on this rank.
    int my_inter_collisions[COLLISION_TYPES]; // Number of collisions that require information from
    // other processors.
    int my_sites; // Number of fluid sites on this rank.
    int shared_fs; // Number of distributions shared with neighbouring
    // processors.
    int *fluid_sites; // Array containing numbers of fluid sites on
    // each process.

    short int *cluster_id;

    DataBlock *data_block; // See comment next to struct DataBlock.
    DataBlock *map_block; // See comment next to struct DataBlock.

    ProcBlock *proc_block; // See comment next to struct ProcBlock.

    WallBlock *wall_block; // See comment next to struct WallBlock.

    NeighProc neigh_proc[NEIGHBOUR_PROCS_MAX]; // See comment next to struct NeighProc.

    unsigned int GetCollisionType(unsigned int site_data);

#ifndef NOMPI
    MPI_Status status[4]; // Define variables for MPI non-blocking sends, receives.

    MPI_Request **req;
#endif
    double dd_time, bm_time, fr_time, fo_time;
    unsigned int *net_site_data;

  private:
    // Some sort of coordinates.
    struct SiteLocation
    {
        short int i, j, k;
    };

    unsigned int *netSiteMapPointer(int site_i, int site_j, int site_k);

    double* cut_distances;
    double* net_site_nor;

    int *machine_id;
    int *procs_per_machine;
    short int *from_proc_id_to_neigh_proc_index; // Turns proc_id to neigh_proc_iindex.

    int mRank; // Processor rank

    // 3 buffers needed for convergence-enabled simulations
    double *f_to_send;
    double *f_to_recv;
    int *f_send_id;
    short int *f_data;
};

// TODO Ugh. Will get rid of these to somewhere else at some point.
extern int sites_x, sites_y, sites_z;
extern int blocks_x, blocks_y, blocks_z;
extern int blocks;
extern int block_size;
extern int shift;
extern int sites_in_a_block;
extern int net_machines;
extern double *f_old, *f_new;
extern int *f_id;
extern int *f_recv_iv;

#endif // HEMELB_NET_H
