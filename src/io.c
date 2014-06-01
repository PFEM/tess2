/*---------------------------------------------------------------------------
 *
 * parallel netcdf I/O for voronoi tesselation
 *
 * Wei-keng Liao (Northwestern University)
 * Tom Peterka 
 * Argonne National Laboratory
 * 9700 S. Cass Ave.
 * Argonne, IL 60439
 * tpeterka@mcs.anl.gov
 *
 * (C) 2013 by Argonne National Laboratory.
 * See COPYRIGHT in top-level directory.
 *
--------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <mpi.h>
#include <pnetcdf.h>
#include "tess/voronoi.h"
#include "diy.h"
#include "tess/io.h"

/*---------------------------------------------------------------------------
 *  pnetcdf delaunay file schema
 *
 *      dimensions:
 *              num_g_blocks; ie, tot_blocks
 *              XYZ = 3;
 *              num_g_orig_particles;
 *              num_g_neighbors;
 *              num_g_tets;
 *              V0V1V2V3 = 4;
 *              num_g_rem_tet_verts;
 *      variables:
 *              int num_orig_particles(num_g_blocks) ;
 *              int num_tets(num_g_blocks) ;
 *              int num_rem_tet_verts(num_g_blocks) ;
 *              int64 block_off_num_orig_particles(num_g_blocks) ;
 *              int64 block_off_num_tets(num_g_blocks) ;
 *              int64 block_off_num_rem_tet_verts(num_g_blocks) ;
 *              int64 block_off_num_neighbors(num_g_blocks) ;
 *              float mins(tot_blocks, XYZ) ;
 *              float maxs(tot_blocks, XYZ) ;
 *              float particles(num_g_orig_particles) ;
 *              int num_neighbors(num_g_blocks) ;
 *              int neighbors(num_g_neighbors) ;
 *              int neigh_procs(num_g_neighbors) ;
 *              int g_block_ids(num_g_blocks) ;
 *              int tets(8 * num_g_tets) ; (verts and neighbors combined)
 *              int rem_tet_verts(num_g_rem_tet_verts) ;
 *              int vert_to_tet(num_g_orig_particles);
 *
---------------------------------------------------------------------------*/
/*
  writes output in pnetcdf format

  nblocks: local number of blocks
  dblocks: pointer to array of dblocks
  out_file: output file name
  comm: MPI communicator
*/
void pnetcdf_d_write(int nblocks, struct dblock_t *dblocks, 
		    char *out_file, MPI_Comm comm) {

  int err;
  int ncid, cmode, varids[41], dimids[14], dimids_2D[2];
  MPI_Offset start[2], count[2];

  MPI_Offset proc_quants[NUM_QUANTS]; /* quantities per process */
  MPI_Offset tot_quants[NUM_QUANTS]; /* total quantities all global blocks */
  MPI_Offset block_ofsts[NUM_QUANTS]; /* starting offsets for each block */

  /* init */
  int i;
  for (i = 0; i < NUM_QUANTS; i++) {
    proc_quants[i] = 0;
    tot_quants[i] = 0;
    block_ofsts[i] = 0;
  }

  /* sum quantities over local blocks */
  int b;
  for (b = 0; b < nblocks; b++) {
    proc_quants[NUM_PARTS] += dblocks[b].num_particles;
    proc_quants[NUM_NEIGHBORS] += DIY_Num_neighbors(0, b);
    /* 2x because I converted array of structs to array of ints */
    proc_quants[NUM_LOC_TETRAS] += 2 * dblocks[b].num_tets;
    proc_quants[NUM_REM_TETRAS] += dblocks[b].num_rem_tet_verts;
  }
  proc_quants[NUM_BLOCKS] = nblocks;

  /* sum per process values to be global ones */
  MPI_Allreduce(proc_quants, tot_quants, NUM_QUANTS, MPI_OFFSET, MPI_SUM, comm);

  /* prefix sum proc offsets */
  MPI_Exscan(proc_quants, &block_ofsts, NUM_QUANTS, MPI_OFFSET, MPI_SUM, comm);

  /* create a new file for writing */
  cmode = NC_CLOBBER | NC_64BIT_DATA;
  err = ncmpi_create(comm, out_file, cmode, MPI_INFO_NULL, &ncid); ERR;

  /* --- define dimensions --- */

  err = ncmpi_def_dim(ncid, "num_g_blocks", tot_quants[NUM_BLOCKS], 
		      &dimids[0]); ERR;
  err = ncmpi_def_dim(ncid, "XYZ", 3, &dimids[1]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_particles", tot_quants[NUM_PARTS], 
		      &dimids[6]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_neighbors", tot_quants[NUM_NEIGHBORS],
		      &dimids[7]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_tets", tot_quants[NUM_LOC_TETRAS],
		      &dimids[8]); ERR;
  err = ncmpi_def_dim(ncid, "V0V1V2V3", 4, &dimids[9]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_rem_tet_verts", 
		      tot_quants[NUM_REM_TETRAS], &dimids[10]); ERR;

  /* --- define variables --- */

  /* quantities */
  err = ncmpi_def_var(ncid, "num_orig_particles", NC_INT, 1, &dimids[0], 
		      &varids[4]); ERR;
  err = ncmpi_def_var(ncid, "num_particles", NC_INT, 1, &dimids[0], 
		      &varids[5]); ERR;
  err = ncmpi_def_var(ncid, "num_tets", NC_INT, 1, &dimids[0], 
		      &varids[25]); ERR;
  err = ncmpi_def_var(ncid, "num_rem_tet_verts", NC_INT, 1, &dimids[0], 
		      &varids[28]); ERR;

  /* block offsets
     encode the offset in the full array of each variable wheree the first
     block of each process starts */
  err = ncmpi_def_var(ncid, "block_off_num_particles", NC_INT64, 1, 
		      &dimids[0], &varids[9]); ERR;
  err = ncmpi_def_var(ncid, "block_off_num_tets", NC_INT64, 1, 
		      &dimids[0], &varids[26]); ERR;
  err = ncmpi_def_var(ncid, "block_off_num_rem_tet_verts", NC_INT64, 1, 
		      &dimids[0], &varids[29]); ERR;
  err = ncmpi_def_var(ncid, "block_off_num_neighbors", NC_INT64, 1,
		      &dimids[0], &varids[40]); ERR;

  /* other data */
  dimids_2D[0] = dimids[0];
  dimids_2D[1] = dimids[1];
  err = ncmpi_def_var(ncid, "mins", NC_FLOAT, 2, dimids_2D, &varids[11]); ERR;
  err = ncmpi_def_var(ncid, "maxs", NC_FLOAT, 2, dimids_2D, &varids[12]); ERR;
  dimids_2D[0] = dimids[6];
  dimids_2D[1] = dimids[1];
  err = ncmpi_def_var(ncid, "particles", NC_FLOAT, 2, dimids_2D, 
		      &varids[14]); ERR;
  err = ncmpi_def_var(ncid, "num_neighbors", NC_INT, 1, &dimids[0], 
		      &varids[24]); ERR;
  err = ncmpi_def_var(ncid, "neighbors", NC_INT, 1, &dimids[7], 
		      &varids[21]); ERR;
  err = ncmpi_def_var(ncid, "neigh_procs", NC_INT, 1, &dimids[7], 
		      &varids[22]); ERR;
  err = ncmpi_def_var(ncid, "g_block_ids", NC_INT, 1, &dimids[0], 
		      &varids[23]); ERR;
  dimids_2D[0] = dimids[8];
  dimids_2D[1] = dimids[9];
  err = ncmpi_def_var(ncid, "tets", NC_INT, 2, dimids_2D, 
		      &varids[27]); ERR;
  err = ncmpi_def_var(ncid, "rem_tet_vert_gids", NC_INT, 1, &dimids[10],
		      &varids[30]); ERR;
  err = ncmpi_def_var(ncid, "rem_tet_vert_nids", NC_INT, 1, &dimids[10], 
		      &varids[31]); ERR;
  err = ncmpi_def_var(ncid, "rem_tet_vert_dirs", NC_UBYTE, 1, &dimids[10], 
		      &varids[32]); ERR;
  err = ncmpi_def_var(ncid, "vert_to_tet", NC_INT, 1, &dimids[6], 
		      &varids[33]); ERR;

  /* exit define mode */
  err = ncmpi_enddef(ncid); ERR;

  /* write all variables.
     to improve: we can try nonblocking I/O to aggregate small requests */

  for (b = 0; b < nblocks; b++) {

    struct dblock_t *d = &dblocks[b];

    /* quantities */
    start[0] = block_ofsts[NUM_BLOCKS];
    count[0] = 1;
    err = ncmpi_put_vara_int_all(ncid, varids[4], start, count, 
				 &d->num_orig_particles); ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[5], start, count, 
				 &d->num_particles); ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[25], start, count, 
				 &d->num_tets); ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[28], start, count, 
				 &d->num_rem_tet_verts); ERR;

    /* block offsets */
    err = ncmpi_put_vara_longlong_all(ncid, varids[9], start, count, 
				      &block_ofsts[NUM_PARTS]); ERR;
    err = ncmpi_put_vara_longlong_all(ncid, varids[26], start, count, 
				      &block_ofsts[NUM_LOC_TETRAS]); ERR;
    err = ncmpi_put_vara_longlong_all(ncid, varids[29], start, count, 
				      &block_ofsts[NUM_REM_TETRAS]); ERR;
    err = ncmpi_put_vara_longlong_all(ncid, varids[40], start, count,
				      &block_ofsts[NUM_NEIGHBORS]); ERR;

    /* block bounds */
    start[0] = block_ofsts[NUM_BLOCKS];
    count[0] = 1;
    start[1] = 0;
    count[1] = 3;
    err = ncmpi_put_vara_float_all(ncid, varids[11], start, count, 
				   d->mins); ERR;
    err = ncmpi_put_vara_float_all(ncid, varids[12], start, count, 
				   d->maxs); ERR;

    /* particles */
    start[0] = block_ofsts[NUM_PARTS];
    start[1] = 0;
    count[0] = d->num_particles;
    count[1] = 3;
    err = ncmpi_put_vara_float_all(ncid, varids[14], start, count, 
				   d->particles); ERR;

    /* num_neighbors, neighbors, neigh_procs */
    int num_neighbors = DIY_Num_neighbors(0, b);
    struct gb_t *neigh_gbs =
      (struct gb_t *)malloc(num_neighbors * sizeof(struct gb_t));
    int *neighbors = (int*)malloc(num_neighbors * sizeof(int));
    int *neigh_procs = (int*)malloc(num_neighbors * sizeof(int));
    DIY_Get_neighbors(0, b, neigh_gbs);
    for (i = 0; i < num_neighbors; i++) {
      neighbors[i] = neigh_gbs[i].gid;
      neigh_procs[i] = neigh_gbs[i].proc;
    }
    start[0] = block_ofsts[NUM_BLOCKS];
    count[0] = 1;
    err = ncmpi_put_vara_int_all(ncid, varids[24], start, count, 
				 &num_neighbors); ERR;
    start[0] = block_ofsts[NUM_NEIGHBORS];
    count[0] = num_neighbors;
    err = ncmpi_put_vara_int_all(ncid, varids[21], start, count, neighbors);
    ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[22], start, count, neigh_procs);
    ERR;

    /* gids */
    int gid = DIY_Gid(0, b);
    start[0] = block_ofsts[NUM_BLOCKS];
    count[0] = 1;
    err = ncmpi_put_vara_int_all(ncid, varids[23], start, count, 
				 &gid); ERR;

    /* tets */

    /* local */
    count[0] = d->num_tets * 2; /* verts and neighbors combined */
    count[1] = (count[0] ? 4 : 0);
    start[0] = (count[0] ? block_ofsts[NUM_LOC_TETRAS] : 0);
    start[1] = 0;
    /* casting array of struct of ints to simple 2D array of ints
       because pnetcdf likes arrays */
    err = ncmpi_put_vara_int_all(ncid, varids[27], start, count,
				 (int *)(d->tets)); ERR;
    /* remote
       an extra memory copy is needed because pnetcdf does not like structs
       with different field types */
    count[0] = d->num_rem_tet_verts;
    start[0] = (count[0] ? block_ofsts[NUM_REM_TETRAS] : 0);
    /* copy individual fields of struct into separate temp. arrays */
    int *ids = (int *)malloc(d->num_rem_tet_verts * sizeof(int));
    for (i = 0; i < d->num_rem_tet_verts; i++)
      ids[i] = d->rem_tet_verts[i].gid;
    err = ncmpi_put_vara_int_all(ncid, varids[30], start, count,
				 ids); ERR;
    for (i = 0; i < d->num_rem_tet_verts; i++)
      ids[i] = d->rem_tet_verts[i].nid;
    err = ncmpi_put_vara_int_all(ncid, varids[31], start, count,
				 ids); ERR;
    free(ids);
    unsigned char *dirs = (unsigned char *)malloc(d->num_rem_tet_verts);
    for (i = 0; i < d->num_rem_tet_verts; i++)
      dirs[i] = d->rem_tet_verts[i].dir;
    err = ncmpi_put_vara_uchar_all(ncid, varids[32], start, count,
				   dirs); ERR;
    free(dirs);

    /* vert_to_tet */
    start[0] = block_ofsts[NUM_PARTS];
    count[0] = d->num_particles;
    err = ncmpi_put_vara_int_all(ncid, varids[33], start, count, 
				 d->vert_to_tet); ERR;

    /* update block offsets */
    block_ofsts[NUM_PARTS] += d->num_particles;
    block_ofsts[NUM_NEIGHBORS] += num_neighbors;
    block_ofsts[NUM_BLOCKS]++;
    /* 2x because I converted array of structs to array of ints */
    block_ofsts[NUM_LOC_TETRAS] += 2 * d->num_tets;
    block_ofsts[NUM_REM_TETRAS] += d->num_rem_tet_verts;

    /* cleanup */
    free(neighbors);
    free(neigh_procs);
    free(neigh_gbs);

  }

  err = ncmpi_close(ncid); ERR;

}
/*--------------------------------------------------------------------------*/
/*
  reads input in pnetcdf format

  nblocks: (output) local number of blocks
  tot_blocks: (output) total number of blocks
  dblocks: (output) pointer to array of dblocks
  in_file: input file name
  comm: MPI communicator
  gids: (output) pointer to array of gids of local blocks 
   (allocated by this function)
  num_neighbors: (output) pointer to array of number of neighbors for 
   each local block (allocated by this function)
  neighbors: (output) pointer to 2D array of gids of neighbors of each 
   local block (allocated by this function)
  neigh_procs: (output) pointer to 2D array of procs of neighbors of each 
   local block (allocated by this function)

  side effects: allocates dblocks, gids, num_neighbors, neighbors, neigh_procs

*/
void pnetcdf_d_read(int *nblocks, int *tot_blocks, struct dblock_t **dblocks, 
		  char *in_file, MPI_Comm comm, int **gids, int **num_neighbors,
		  int ***neighbors, int ***neigh_procs) {

  int err;
  int ncid, varids[41], dimids[14];
  MPI_Offset start[2], count[2];
  nc_type type;
  int ndims, natts;
  int rank, groupsize; /* MPI usual */
  int i;

  /* open file for reading */
  err = ncmpi_open(comm, in_file, NC_NOWRITE, MPI_INFO_NULL, &ncid); ERR;

  err = ncmpi_inq_varid(ncid, "block_off_num_particles", &varids[9]); ERR;
  err = ncmpi_inq_varid(ncid, "block_off_num_tets", &varids[26]); ERR;
  err = ncmpi_inq_varid(ncid, "block_off_num_rem_tet_verts", &varids[29]); ERR;
  err = ncmpi_inq_varid(ncid, "block_off_num_neighbors", &varids[40]); ERR;

  /* get number of blocks */
  MPI_Offset num_g_blocks; /* 64 bit version of tot_blcoks */
  err = ncmpi_inq_varid(ncid, "mins", &varids[11]); ERR;
  err = ncmpi_inq_var(ncid, varids[11], 0, &type, &ndims, 
		      dimids, &natts); ERR;
  err = ncmpi_inq_dimlen(ncid, dimids[0], &num_g_blocks); ERR;
  *tot_blocks = num_g_blocks;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &groupsize);

  /* todo: following only works for same number of blocks for all procs */
  int bp = *tot_blocks / groupsize; /* nominal blocks per process */
  int start_block_ofst =  rank * bp;
  /* end of restriction */

  *nblocks = (rank < groupsize - 1 ? bp : *tot_blocks - (rank * bp));

  /* all gids and block index in file of each gid
     todo: not scalable*/
  int *all_gids = (int *)malloc(*tot_blocks * sizeof(int));
  int *gid2idx = (int *)malloc(*tot_blocks * sizeof(int));
  start[0] = 0;
  count[0] = *tot_blocks;
  err = ncmpi_inq_varid(ncid, "g_block_ids", &varids[23]); ERR;
  err = ncmpi_get_vara_int_all(ncid, varids[23], start, count, 
			       all_gids); ERR;
  for (i = 0; i < *tot_blocks; i++)
    gid2idx[all_gids[i]] = i;

  /* block offsets */
  int64_t *block_ofsts = (int64_t*)malloc(*tot_blocks * sizeof(int64_t));
  *dblocks = (struct dblock_t*)malloc(*nblocks * sizeof(struct dblock_t));

  /* read all blocks */
  *gids = (int *)malloc(*nblocks * sizeof(int));
  *num_neighbors = (int *)malloc(*nblocks * sizeof(int));
  *neighbors = (int **)malloc(*nblocks * sizeof(int *));
  *neigh_procs = (int **)malloc(*nblocks * sizeof(int *));

  /* for all blocks */
  int b;
  for (b = 0; b < *nblocks; b++) {

    struct dblock_t *d = &((*dblocks)[b]);

    /* quantities */
    start[0] = start_block_ofst + b;
    count[0] = 1;
    err = ncmpi_inq_varid(ncid, "num_orig_particles", &varids[4]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[4], start, count, 
				 &(d->num_orig_particles)); ERR;
    err = ncmpi_inq_varid(ncid, "num_particles", &varids[5]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[5], start, count, 
				 &(d->num_particles)); ERR;
    err = ncmpi_inq_varid(ncid, "num_tets", &varids[25]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[25], start, count, 
				 &(d->num_tets)); ERR;
    err = ncmpi_inq_varid(ncid, "num_rem_tet_verts", &varids[28]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[28], start, count, 
				 &(d->num_rem_tet_verts)); ERR;

    /* block bounds */
    start[0] = start_block_ofst + b;
    start[1] = 0;
    count[0] = 1;
    count[1] = 3;
    err = ncmpi_inq_varid(ncid, "mins", &varids[11]); ERR;
    err = ncmpi_get_vara_float_all(ncid, varids[11], start, count, 
				   d->mins); ERR;
    err = ncmpi_inq_varid(ncid, "maxs", &varids[12]); ERR;
    err = ncmpi_get_vara_float_all(ncid, varids[12], start, count, 
				   d->maxs); ERR;

    /* particles */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[9], start, count, 
				      (long long *)block_ofsts); ERR;
    d->particles = 
      (float *)malloc(d->num_particles * 3 * sizeof(float));
    start[0] = block_ofsts[start_block_ofst + b];
    start[1] = 0;
    count[0] = d->num_particles;
    count[1] = 3;
    err = ncmpi_inq_varid(ncid, "particles", &varids[14]); ERR;
    err = ncmpi_get_vara_float_all(ncid, varids[14], start, count, 
				   d->particles); ERR;

    /* neighbors */
    err = ncmpi_inq_varid(ncid, "neighbors", &varids[21]); ERR;
    err = ncmpi_inq_varid(ncid, "num_neighbors", &varids[24]); ERR;
    start[0] = start_block_ofst + b;
    count[0] = 1;
    err = ncmpi_get_vara_int_all(ncid, varids[24], start, count, 
				 &((*num_neighbors)[b])); ERR;
    if ((*num_neighbors)[b]) {
      (*neighbors)[b] = (int *)malloc((*num_neighbors)[b] * sizeof(int));
      (*neigh_procs)[b] = (int *)malloc((*num_neighbors)[b] * sizeof(int));
      start[0] = 0;
      count[0] = *tot_blocks;
      err = ncmpi_get_vara_longlong_all(ncid, varids[40], start, count, 
					(long long *) block_ofsts); ERR;
      start[0] = block_ofsts[start_block_ofst + b];
      count[0] = (*num_neighbors)[b];
      err = ncmpi_get_vara_int_all(ncid, varids[21], start, count, 
				   (*neighbors)[b]); ERR;
    }

    /* neigh_procs is not read from the file; rather distributed anew
       according to the number of processes being used now
       for now relies on blocks being in gid order in file */
    int n;
    for (n = 0; n < (*num_neighbors)[b]; n++) {
      (*neigh_procs)[b][n] = gid2idx[(*neighbors)[b][n]] / bp;
      if ((*neigh_procs)[b][n] >= groupsize)
	(*neigh_procs[b][n] = groupsize - 1);
    }

    /* gids */
    (*gids)[b] = all_gids[start_block_ofst + b];

    /* tets */
    /* local */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[26], start, count, 
				      (long long *) block_ofsts); ERR;
    d->tets = 
      (struct tet_t*)malloc(d->num_tets * sizeof(struct tet_t));
    /* 2x because I converted array of structs to array of ints */
    count[0] = 2 * d->num_tets;
    count[1] = (count[0] ? 4 : 0);
    start[0] = (count[0] ? block_ofsts[start_block_ofst + b] : 0);
    start[1] = 0;
    err = ncmpi_inq_varid(ncid, "tets", &varids[27]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[27], start, count, 
				 (int *)d->tets); ERR;
    /* remote
       an extra memory copy is needed because pnetcdf does not like structs
       with different field types */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[29], start, count,
				      (long long *) block_ofsts); ERR;
    if (d->num_rem_tet_verts)
      d->rem_tet_verts =
	(struct remote_vert_t *)malloc(d->num_rem_tet_verts *
				       sizeof(struct remote_vert_t));
    count[0] = d->num_rem_tet_verts;
    start[0] = (count[0] ? block_ofsts[start_block_ofst + b] : 0);
    /* copy individual fields of struct into seaparate temp. arrays */
    int *ids =  NULL;
    if (d->num_rem_tet_verts)
      ids = (int *)malloc(d->num_rem_tet_verts * sizeof(int));
    err = ncmpi_inq_varid(ncid, "rem_tet_vert_gids", &varids[30]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[30], start, count,
				 ids); ERR;
    for (i = 0; i < d->num_rem_tet_verts; i++)
      d->rem_tet_verts[i].gid = ids[i];
    err = ncmpi_inq_varid(ncid, "rem_tet_vert_nids", &varids[31]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[31], start, count,
				 ids); ERR;
    for (i = 0; i < d->num_rem_tet_verts; i++)
      d->rem_tet_verts[i].nid = ids[i];
    if (d->num_rem_tet_verts)
      free(ids);
    unsigned char *dirs = NULL;
    if (d->num_rem_tet_verts)
      dirs = (unsigned char *)malloc(d->num_rem_tet_verts);
    err = ncmpi_inq_varid(ncid, "rem_tet_vert_dirs", &varids[32]); ERR;
    err = ncmpi_get_vara_uchar_all(ncid, varids[32], start, count,
				   dirs); ERR;
    for (i = 0; i < d->num_rem_tet_verts; i++)
      d->rem_tet_verts[i].dir = dirs[i];
    if (d->num_rem_tet_verts)
      free(dirs);
    
    /* vert_to_tet */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[9], start, count, 
				      (long long *)block_ofsts); ERR;
    d->vert_to_tet = 
      (int *)malloc(d->num_particles * sizeof(int));
    start[0] = block_ofsts[start_block_ofst + b];
    count[0] = d->num_particles;
    err = ncmpi_inq_varid(ncid, "vert_to_tet", &varids[33]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[33], start, count, 
				   d->vert_to_tet); ERR;

  } /* for all blocks */

  /* cleanup */
  err = ncmpi_close(ncid); ERR;
  free(block_ofsts);
  free(all_gids);
  free(gid2idx);

}
/*--------------------------------------------------------------------------*/
/*
  creates DIY datatype for the subset of the delaunay block to write to disk

  dblock: voronoi block
  did: domain id (unused in this case)
  lid: local block number (unused in this case)
  dtype: pointer to datatype

  side effects: commits datatype

*/
void create_d_datatype(void* dblock, int did, int lid, DIY_Datatype *dtype) {

  did = did; /* quiet compiler warning */
  lid = lid; 

  /* datatype for tet */
  DIY_Datatype ttype;
  struct map_block_t tet_map[] = {

    {DIY_INT,    OFST, 4,
     offsetof(struct tet_t, verts) },
    {DIY_INT,    OFST, 4,
     offsetof(struct tet_t, tets)  },

  };
  DIY_Create_struct_datatype(0, 2, tet_map, &ttype);

  /* datatype for remote tet vert */
  DIY_Datatype rtype;
  struct map_block_t rem_map[] = {

    {DIY_INT,    OFST, 1,
     offsetof(struct remote_vert_t, gid) },
    {DIY_INT,    OFST, 1,
     offsetof(struct remote_vert_t, nid) },
    {DIY_BYTE,   OFST, 1,
     offsetof(struct remote_vert_t, dir) },

  };
  DIY_Create_struct_datatype(0, 3, rem_map, &rtype);

  struct dblock_t *d = (struct dblock_t *)dblock;
  struct map_block_t map[] = {

    { DIY_FLOAT, OFST, 3, 
      offsetof(struct dblock_t, mins)             },
    { DIY_FLOAT, ADDR, d->num_particles * 3,
      DIY_Addr(d->particles)                      },
    { ttype,     ADDR, d->num_tets, 
      DIY_Addr(d->tets)                           },
    { rtype,     ADDR, d->num_rem_tet_verts, 
      DIY_Addr(d->rem_tet_verts)                  },
    { DIY_INT,   ADDR, d->num_particles,
      DIY_Addr(d->vert_to_tet)                    },
    { DIY_FLOAT, OFST, 3, 
      offsetof(struct dblock_t, maxs)             },

  };

  DIY_Create_struct_datatype(DIY_Addr(dblock), 6, map, dtype);

  DIY_Destroy_datatype(&ttype);
  DIY_Destroy_datatype(&rtype);

}
/*--------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
 *  pnetcdf voronoi and delaunay file schema
 *
 *      dimensions:
 *              num_g_blocks; ie, tot_blocks
 *              XYZ = 3;
 *              num_g_verts;
 *              num_g_complete_cells;
 *              tot_num_g_cell_faces;
 *              num_g_orig_particles;
 *              num_g_neighbors;
 *              num_g_loc_tets;
 *              V0V1V2V3 = 4;
 *              num_g_rem_tets;
 *              num_g_faces;
 *              vface_t = 3 + MAX_VERTS;
 *      variables:
 *              int num_verts(num_g_blocks) ;
 *              int num_complete_cells(num_g_blocks) ;
 *              int tot_num_cell_faces(num_g_blocks) ;
 *              int num_orig_particles(num_g_blocks) ;
 *              int num_loc_tets(num_g_blocks) ;
 *              int num_rem_tets(num_g_blocks) ;
 *              int num_faces(num_g_blocks) ;
 *              int64 block_off_num_verts(num_g_blocks) ;
 *              int64 block_off_num_complete_cells(num_g_blocks) ;
 *              int64 block_off_tot_num_cell_faces(num_g_blocks) ;
 *              int64 block_off_num_orig_particles(num_g_blocks) ;
 *              int64 block_off_num_loc_tets(num_g_blocks) ;
 *              int64 block_off_num_rem_tets(num_g_blocks) ;
 *              int64 block_off_num_faces(num_g_blocks) ;
 *              int64 block_off_num_neighbors(num_g_blocks) ;
 *              float mins(tot_blocks, XYZ) ;
 *              float maxs(tot_blocks, XYZ) ;
 *              float save_verts(num_g_verts, XYZ) ;
 *              float sites(num_g_orig_particles) ;
 *              int complete_cells(num_g_complete_cells) ;
 *              float areas(num_g_complete_cells) ;
 *              float vols(num_g_complete_cells) ;
 *              int num_cell_faces(num_g_complete_cells) ;
 *              int num_neighbors(num_g_blocks) ;
 *              int neighbors(num_g_neighbors) ;
 *              int neigh_procs(num_g_neighbors) ;
 *              int g_block_ids(num_g_blocks) ;
 *              int loc_tets(num_g_loc_tets, V0V1V2V3) ;
 *              int rem_tet_gids(num_g_rem_tets, V0V1V2V3) ;
 *              int rem_tet_nids(num_g_rem_tets, V0V1V2V3) ;
 *              unsigned char rem_tet_wrap_dirs(num_g_rem_tets, V0V1V2V3) ;
 *
---------------------------------------------------------------------------*/
/*
  writes output in pnetcdf format

  nblocks: local number of blocks
  vblocks: pointer to array of vblocks
  out_file: output file name
  comm: MPI communicator
*/
void pnetcdf_write(int nblocks, struct vblock_t *vblocks, 
		   char *out_file, MPI_Comm comm) {

  int err;
  int ncid, cmode, varids[41], dimids[14], dimids_2D[2];
  MPI_Offset start[2], count[2];

  MPI_Offset proc_quants[NUM_QUANTS]; /* quantities per process */
  MPI_Offset tot_quants[NUM_QUANTS]; /* total quantities all global blocks */
  MPI_Offset block_ofsts[NUM_QUANTS]; /* starting offsets for each block */

  /* init */
  int i;
  for (i = 0; i < NUM_QUANTS; i++) {
    proc_quants[i] = 0;
    tot_quants[i] = 0;
    block_ofsts[i] = 0;
  }

  /* sum quantities over local blocks */
  int b;
  for (b = 0; b < nblocks; b++) {
    proc_quants[NUM_VERTICES] += vblocks[b].num_verts;
    proc_quants[NUM_COMP_CELLS] += vblocks[b].num_complete_cells;
    proc_quants[NUM_CELL_FACES] += vblocks[b].tot_num_cell_faces;
    proc_quants[NUM_ORIG_PARTS] += vblocks[b].num_orig_particles;
    proc_quants[NUM_NEIGHBORS] += DIY_Num_neighbors(0, b);
    proc_quants[NUM_LOC_TETRAS] += vblocks[b].num_loc_tets;
    proc_quants[NUM_REM_TETRAS] += vblocks[b].num_rem_tets;
    proc_quants[NUM_UNIQUE_FACES] += vblocks[b].num_faces;
  }
  proc_quants[NUM_BLOCKS] = nblocks;

  /* sum per process values to be global ones */
  MPI_Allreduce(proc_quants, tot_quants, NUM_QUANTS, MPI_OFFSET, MPI_SUM, comm);

  /* prefix sum proc offsets */
  MPI_Exscan(proc_quants, &block_ofsts, NUM_QUANTS, MPI_OFFSET, MPI_SUM, comm);

  /* create a new file for writing */
  cmode = NC_CLOBBER | NC_64BIT_DATA;
  err = ncmpi_create(comm, out_file, cmode, MPI_INFO_NULL, &ncid); ERR;

  /* --- define dimensions --- */

  err = ncmpi_def_dim(ncid, "num_g_blocks", tot_quants[NUM_BLOCKS], 
		      &dimids[0]); ERR;
  err = ncmpi_def_dim(ncid, "XYZ", 3, &dimids[1]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_verts", tot_quants[NUM_VERTICES],
		      &dimids[2]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_complete_cells", tot_quants[NUM_COMP_CELLS],
		      &dimids[3]); ERR;
  err = ncmpi_def_dim(ncid, "tot_num_g_cell_faces", tot_quants[NUM_CELL_FACES],
		      &dimids[4]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_orig_particles", tot_quants[NUM_ORIG_PARTS], 
		      &dimids[6]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_neighbors", tot_quants[NUM_NEIGHBORS],
		      &dimids[7]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_loc_tets", tot_quants[NUM_LOC_TETRAS],
		      &dimids[8]); ERR;
  err = ncmpi_def_dim(ncid, "V0V1V2V3", 4, &dimids[9]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_rem_tets", tot_quants[NUM_REM_TETRAS],
		      &dimids[10]); ERR;
  err = ncmpi_def_dim(ncid, "num_g_faces", tot_quants[NUM_UNIQUE_FACES],
		      &dimids[11]); ERR;
  err = ncmpi_def_dim(ncid, "vface_t", 3 + MAX_FACE_VERTS, &dimids[13]); ERR;

  /* --- define variables --- */

  /* quantities */
  err = ncmpi_def_var(ncid, "num_verts", NC_INT, 1, &dimids[0], 
		      &varids[0]); ERR;
  err = ncmpi_def_var(ncid, "num_complete_cells", NC_INT, 1, &dimids[0], 
		      &varids[1]); ERR;
  err = ncmpi_def_var(ncid, "tot_num_cell_faces", NC_INT, 1, &dimids[0], 
		      &varids[2]); ERR;
  err = ncmpi_def_var(ncid, "num_orig_particles", NC_INT, 1, &dimids[0], 
		      &varids[4]); ERR;
  err = ncmpi_def_var(ncid, "num_loc_tets", NC_INT, 1, &dimids[0], 
		      &varids[25]); ERR;
  err = ncmpi_def_var(ncid, "num_rem_tets", NC_INT, 1, &dimids[0], 
		      &varids[28]); ERR;
  err = ncmpi_def_var(ncid, "num_faces", NC_INT, 1, &dimids[0],
		      &varids[33]); ERR;

  /* block offsets
     encode the offset in the full array of each variable wheree the first
     block of each process starts */
  err = ncmpi_def_var(ncid, "block_off_num_verts", NC_INT64, 1, &dimids[0], 
		      &varids[5]); ERR;
  err = ncmpi_def_var(ncid, "block_off_num_complete_cells", NC_INT64, 1, 
		      &dimids[0], &varids[6]); ERR;
  err = ncmpi_def_var(ncid, "block_off_tot_num_cell_faces", NC_INT64, 1, 
		      &dimids[0], &varids[7]); ERR;
  err = ncmpi_def_var(ncid, "block_off_num_orig_particles", NC_INT64, 1, 
		      &dimids[0], &varids[9]); ERR;
  err = ncmpi_def_var(ncid, "block_off_num_loc_tets", NC_INT64, 1, 
		      &dimids[0], &varids[26]); ERR;
  err = ncmpi_def_var(ncid, "block_off_num_rem_tets", NC_INT64, 1, 
		      &dimids[0], &varids[29]); ERR;
  err = ncmpi_def_var(ncid, "block_off_num_faces", NC_INT64, 1,
		      &dimids[0], &varids[35]); ERR;
  err = ncmpi_def_var(ncid, "block_off_num_neighbors", NC_INT64, 1,
		      &dimids[0], &varids[40]); ERR;

  /* other data */
  dimids_2D[0] = dimids[0];
  dimids_2D[1] = dimids[1];
  err = ncmpi_def_var(ncid, "mins", NC_FLOAT, 2, dimids_2D, &varids[11]); ERR;
  err = ncmpi_def_var(ncid, "maxs", NC_FLOAT, 2, dimids_2D, &varids[12]); ERR;
  dimids_2D[0] = dimids[2];
  dimids_2D[1] = dimids[1];
  err = ncmpi_def_var(ncid, "save_verts", NC_FLOAT, 2, dimids_2D, 
		      &varids[13]); ERR;
  dimids_2D[0] = dimids[6];
  dimids_2D[1] = dimids[1];
  err = ncmpi_def_var(ncid, "sites", NC_FLOAT, 2, dimids_2D, 
		      &varids[14]); ERR;
  err = ncmpi_def_var(ncid, "complete_cells", NC_INT, 1, &dimids[3], 
		      &varids[15]); ERR;
  err = ncmpi_def_var(ncid, "areas", NC_FLOAT, 1, &dimids[3], 
		      &varids[16]); ERR;
  err = ncmpi_def_var(ncid, "vols", NC_FLOAT, 1, &dimids[3], &varids[17]); ERR;
  err = ncmpi_def_var(ncid, "num_cell_faces", NC_INT, 1, &dimids[3], 
		      &varids[18]); ERR;
  err = ncmpi_def_var(ncid, "num_neighbors", NC_INT, 1, &dimids[0], 
		      &varids[24]); ERR;
  err = ncmpi_def_var(ncid, "neighbors", NC_INT, 1, &dimids[7], 
		      &varids[21]); ERR;
  err = ncmpi_def_var(ncid, "neigh_procs", NC_INT, 1, &dimids[7], 
		      &varids[22]); ERR;
  err = ncmpi_def_var(ncid, "g_block_ids", NC_INT, 1, &dimids[0], 
		      &varids[23]); ERR;
  dimids_2D[0] = dimids[8];
  dimids_2D[1] = dimids[9];
  err = ncmpi_def_var(ncid, "loc_tets", NC_INT, 2, dimids_2D, 
		      &varids[27]); ERR;
  dimids_2D[0] = dimids[10];
  err = ncmpi_def_var(ncid, "rem_tet_gids", NC_INT, 2, dimids_2D, 
		      &varids[30]); ERR;
  err = ncmpi_def_var(ncid, "rem_tet_nids", NC_INT, 2, dimids_2D, 
		      &varids[31]); ERR;
  err = ncmpi_def_var(ncid, "rem_tet_wrap_dirs", NC_UBYTE, 2, dimids_2D, 
		      &varids[32]); ERR;
  dimids_2D[0] = dimids[11];
  dimids_2D[1] = dimids[13];
  err = ncmpi_def_var(ncid, "faces", NC_INT, 2, dimids_2D, 
		      &varids[37]); ERR;
  err = ncmpi_def_var(ncid, "cell_faces_start", NC_INT, 1, &dimids[6], 
		      &varids[38]); ERR;
  err = ncmpi_def_var(ncid, "cell_faces", NC_INT, 1, &dimids[4], 
		      &varids[39]); ERR;

  /* exit define mode */
  err = ncmpi_enddef(ncid); ERR;

  /* write all variables.
     to improve: we can try nonblocking I/O to aggregate small requests */

  for (b = 0; b < nblocks; b++) {

    struct vblock_t *v = &vblocks[b];

    /* quantities */
    start[0] = block_ofsts[NUM_BLOCKS];
    count[0] = 1;
    err = ncmpi_put_vara_int_all(ncid, varids[0], start, count, 
				 &v->num_verts); ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[1], start, count, 
				 &v->num_complete_cells); ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[2], start, count, 
				 &v->tot_num_cell_faces); ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[4], start, count, 
				 &v->num_orig_particles); ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[25], start, count, 
				 &v->num_loc_tets); ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[28], start, count, 
				 &v->num_rem_tets); ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[33], start, count,
				 &v->num_faces); ERR;

    /* block offsets */
    err = ncmpi_put_vara_longlong_all(ncid, varids[5], start, count, 
				      &block_ofsts[NUM_VERTICES]); ERR;
    err = ncmpi_put_vara_longlong_all(ncid, varids[6], start, count, 
				      &block_ofsts[NUM_COMP_CELLS]); ERR;
    err = ncmpi_put_vara_longlong_all(ncid, varids[7], start, count, 
				      &block_ofsts[NUM_CELL_FACES]); ERR;
    err = ncmpi_put_vara_longlong_all(ncid, varids[9], start, count, 
				      &block_ofsts[NUM_ORIG_PARTS]); ERR;
    err = ncmpi_put_vara_longlong_all(ncid, varids[26], start, count, 
				      &block_ofsts[NUM_LOC_TETRAS]); ERR;
    err = ncmpi_put_vara_longlong_all(ncid, varids[29], start, count, 
				      &block_ofsts[NUM_REM_TETRAS]); ERR;
    err = ncmpi_put_vara_longlong_all(ncid, varids[35], start, count,
				      &block_ofsts[NUM_UNIQUE_FACES]); ERR;
    err = ncmpi_put_vara_longlong_all(ncid, varids[40], start, count,
				      &block_ofsts[NUM_NEIGHBORS]); ERR;

    /* block bounds */
    start[0] = block_ofsts[NUM_BLOCKS];
    count[0] = 1;
    start[1] = 0;
    count[1] = 3;
    err = ncmpi_put_vara_float_all(ncid, varids[11], start, count, 
				   v->mins); ERR;
    err = ncmpi_put_vara_float_all(ncid, varids[12], start, count, 
				   v->maxs); ERR;

    /* save_verts */
    start[0] = block_ofsts[NUM_VERTICES];
    start[1] = 0;
    count[0] = v->num_verts;
    count[1] = 3;
    err = ncmpi_put_vara_float_all(ncid, varids[13], start, count,
				   v->save_verts); ERR;

    /* sites */
    start[0] = block_ofsts[NUM_ORIG_PARTS];
    start[1] = 0;
    count[0] = v->num_orig_particles;
    count[1] = 3;
    err = ncmpi_put_vara_float_all(ncid, varids[14], start, count, 
				   v->sites); ERR;

    /* complete cells */
    start[0] = block_ofsts[NUM_COMP_CELLS];
    count[0] = v->num_complete_cells;
    err = ncmpi_put_vara_int_all(ncid, varids[15], start, count, 
				 v->complete_cells); ERR;

    /* areas */
    start[0] = block_ofsts[NUM_COMP_CELLS];
    count[0] = v->num_complete_cells;
    err = ncmpi_put_vara_float_all(ncid, varids[16], start, count, 
				   v->areas); ERR;

    /* volumes */
    start[0] = block_ofsts[NUM_COMP_CELLS];
    count[0] = v->num_complete_cells;
    err = ncmpi_put_vara_float_all(ncid, varids[17], start, count, 
				   v->vols); ERR;

    /* num_neighbors, neighbors, neigh_procs */
    int num_neighbors = DIY_Num_neighbors(0, b);
    struct gb_t *neigh_gbs =
      (struct gb_t *)malloc(num_neighbors * sizeof(struct gb_t));
    int *neighbors = (int*)malloc(num_neighbors * sizeof(int));
    int *neigh_procs = (int*)malloc(num_neighbors * sizeof(int));
    DIY_Get_neighbors(0, b, neigh_gbs);
    for (i = 0; i < num_neighbors; i++) {
      neighbors[i] = neigh_gbs[i].gid;
      neigh_procs[i] = neigh_gbs[i].proc;
    }
    start[0] = block_ofsts[NUM_BLOCKS];
    count[0] = 1;
    err = ncmpi_put_vara_int_all(ncid, varids[24], start, count, 
				 &num_neighbors); ERR;
    start[0] = block_ofsts[NUM_NEIGHBORS];
    count[0] = num_neighbors;
    err = ncmpi_put_vara_int_all(ncid, varids[21], start, count, neighbors);
    ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[22], start, count, neigh_procs);
    ERR;

    /* gids */
    int gid = DIY_Gid(0, b);
    start[0] = block_ofsts[NUM_BLOCKS];
    count[0] = 1;
    err = ncmpi_put_vara_int_all(ncid, varids[23], start, count, 
				 &gid); ERR;

    /* tets */
    /* local */
    count[0] = v->num_loc_tets;
    count[1] = (count[0] ? 4 : 0);
    start[0] = (count[0] ? block_ofsts[NUM_LOC_TETRAS] : 0);
    start[1] = 0;
    err = ncmpi_put_vara_int_all(ncid, varids[27], start, count,
				 v->loc_tets); ERR;
    /* remote */
    count[0] = v->num_rem_tets;
    count[1] = (count[0] ? 4 : 0);
    start[0] = (count[0] ? block_ofsts[NUM_REM_TETRAS] : 0);
    start[1] = 0;
    err = ncmpi_put_vara_int_all(ncid, varids[30], start, count,
				 v->rem_tet_gids); ERR;
    err = ncmpi_put_vara_int_all(ncid, varids[31], start, count,
				 v->rem_tet_nids); ERR;
    err = ncmpi_put_vara_uchar_all(ncid, varids[32], start, count,
				   v->rem_tet_wrap_dirs); ERR;

    /* voronoi faces */
    start[0] = block_ofsts[NUM_UNIQUE_FACES];
    start[1] = 0;
    count[0] = v->num_faces;
    count[1] = 3 + MAX_FACE_VERTS;
    err = ncmpi_put_vara_int_all(ncid, varids[37], start, count,
				 (int *)(v->faces)); ERR;
    start[0] = block_ofsts[NUM_ORIG_PARTS];
    count[0] = v->num_orig_particles;
    err = ncmpi_put_vara_int_all(ncid, varids[38], start, count,
				 v->cell_faces_start); ERR;
    start[0] = block_ofsts[NUM_CELL_FACES];
    count[0] = v->tot_num_cell_faces;
    err = ncmpi_put_vara_int_all(ncid, varids[39], start, count,
				 v->cell_faces); ERR;

    /* update block offsets */
    block_ofsts[NUM_VERTICES] += v->num_verts;
    block_ofsts[NUM_COMP_CELLS] += v->num_complete_cells;
    block_ofsts[NUM_CELL_FACES] += v->tot_num_cell_faces;
    block_ofsts[NUM_ORIG_PARTS] += v->num_orig_particles;
    block_ofsts[NUM_NEIGHBORS] += num_neighbors;
    block_ofsts[NUM_BLOCKS]++;
    block_ofsts[NUM_LOC_TETRAS] += v->num_loc_tets;
    block_ofsts[NUM_REM_TETRAS] += v->num_rem_tets;
    block_ofsts[NUM_UNIQUE_FACES] += v->num_faces;

    /* cleanup */
    free(neighbors);
    free(neigh_procs);
    free(neigh_gbs);

  }

  err = ncmpi_close(ncid); ERR;

}
/*--------------------------------------------------------------------------*/
/*
  reads input in pnetcdf format

  nblocks: (output) local number of blocks
  tot_blocks: (output) total number of blocks
  vblocks: (output) pointer to array of vblocks
  in_file: input file name
  comm: MPI communicator
  gids: (output) pointer to array of gids of local blocks 
   (allocated by this function)
  num_neighbors: (output) pointer to array of number of neighbors for 
   each local block (allocated by this function)
  neighbors: (output) pointer to 2D array of gids of neighbors of each 
   local block (allocated by this function)
  neigh_procs: (output) pointer to 2D array of procs of neighbors of each 
   local block (allocated by this function)

  side effects: allocates vblocks, gids, num_neighbors, neighbors, neigh_procs

*/
void pnetcdf_read(int *nblocks, int *tot_blocks, struct vblock_t **vblocks, 
		  char *in_file, MPI_Comm comm, int **gids, int **num_neighbors,
		  int ***neighbors, int ***neigh_procs) {

  int err;
  int ncid, varids[41], dimids[14];
  MPI_Offset start[2], count[2];
  nc_type type;
  int ndims, natts;
  int rank, groupsize; /* MPI usual */
  int i;

  /* open file for reading */
  err = ncmpi_open(comm, in_file, NC_NOWRITE, MPI_INFO_NULL, &ncid); ERR;

  err = ncmpi_inq_varid(ncid, "block_off_num_verts", &varids[5]); ERR;
  err = ncmpi_inq_varid(ncid, "block_off_num_complete_cells", &varids[6]); ERR;
  err = ncmpi_inq_varid(ncid, "block_off_tot_num_cell_faces", &varids[7]); ERR;
  err = ncmpi_inq_varid(ncid, "block_off_num_orig_particles", &varids[9]); ERR;
  err = ncmpi_inq_varid(ncid, "block_off_num_loc_tets", &varids[26]); ERR;
  err = ncmpi_inq_varid(ncid, "block_off_num_rem_tets", &varids[29]); ERR;
  err = ncmpi_inq_varid(ncid, "block_off_num_faces", &varids[35]); ERR;
  err = ncmpi_inq_varid(ncid, "block_off_num_neighbors", &varids[40]); ERR;

  /* get number of blocks */
  MPI_Offset num_g_blocks; /* 64 bit version of tot_blcoks */
  err = ncmpi_inq_varid(ncid, "mins", &varids[11]); ERR;
  err = ncmpi_inq_var(ncid, varids[11], 0, &type, &ndims, 
		      dimids, &natts); ERR;
  err = ncmpi_inq_dimlen(ncid, dimids[0], &num_g_blocks); ERR;
  *tot_blocks = num_g_blocks;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &groupsize);

  /* todo: following only works for same number of blocks for all procs */
  int bp = *tot_blocks / groupsize; /* nominal blocks per process */
  int start_block_ofst =  rank * bp;
  /* end of restriction */

  *nblocks = (rank < groupsize - 1 ? bp : *tot_blocks - (rank * bp));

  /* all gids and block index in file of each gid
     todo: not scalable*/
  int *all_gids = (int *)malloc(*tot_blocks * sizeof(int));
  int *gid2idx = (int *)malloc(*tot_blocks * sizeof(int));
  start[0] = 0;
  count[0] = *tot_blocks;
  err = ncmpi_inq_varid(ncid, "g_block_ids", &varids[23]); ERR;
  err = ncmpi_get_vara_int_all(ncid, varids[23], start, count, 
			       all_gids); ERR;
  for (i = 0; i < *tot_blocks; i++)
    gid2idx[all_gids[i]] = i;

  /* block offsets */
  int64_t *block_ofsts = (int64_t*)malloc(*tot_blocks * sizeof(int64_t));
  *vblocks = (struct vblock_t*)malloc(*nblocks * sizeof(struct vblock_t));

  /* read all blocks */
  *gids = (int *)malloc(*nblocks * sizeof(int));
  *num_neighbors = (int *)malloc(*nblocks * sizeof(int));
  *neighbors = (int **)malloc(*nblocks * sizeof(int *));
  *neigh_procs = (int **)malloc(*nblocks * sizeof(int *));

  /* for all blocks */
  int b;
  for (b = 0; b < *nblocks; b++) {

    struct vblock_t *v = &((*vblocks)[b]);

    /* quantities */
    start[0] = start_block_ofst + b;
    count[0] = 1;
    err = ncmpi_inq_varid(ncid, "num_verts", &varids[0]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[0], start, count, 
				 &(v->num_verts)); ERR;
    err = ncmpi_inq_varid(ncid, "num_complete_cells", &varids[1]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[1], start, count, 
				 &(v->num_complete_cells)); ERR;
    err = ncmpi_inq_varid(ncid, "tot_num_cell_faces", &varids[2]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[2], start, count, 
				 &(v->tot_num_cell_faces)); ERR;
    err = ncmpi_inq_varid(ncid, "num_orig_particles", &varids[4]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[4], start, count, 
				 &(v->num_orig_particles)); ERR;
    err = ncmpi_inq_varid(ncid, "num_loc_tets", &varids[25]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[25], start, count, 
				 &(v->num_loc_tets)); ERR;
    err = ncmpi_inq_varid(ncid, "num_rem_tets", &varids[28]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[28], start, count, 
				 &(v->num_rem_tets)); ERR;
    err = ncmpi_inq_varid(ncid, "num_faces", &varids[33]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[33], start, count, 
				 &(v->num_faces)); ERR;

    /* block bounds */
    start[0] = start_block_ofst + b;
    start[1] = 0;
    count[0] = 1;
    count[1] = 3;
    err = ncmpi_inq_varid(ncid, "mins", &varids[11]); ERR;
    err = ncmpi_get_vara_float_all(ncid, varids[11], start, count, 
				   v->mins); ERR;
    err = ncmpi_inq_varid(ncid, "maxs", &varids[12]); ERR;
    err = ncmpi_get_vara_float_all(ncid, varids[12], start, count, 
				   v->maxs); ERR;

    /* save_verts */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[5], start, count, 
				      (long long *)block_ofsts); ERR;
    v->save_verts = (float *)malloc(v->num_verts * 3 * sizeof(float));
    start[0] = block_ofsts[start_block_ofst + b];
    start[1] = 0;
    count[0] = v->num_verts;
    count[1] = 3;
    err = ncmpi_inq_varid(ncid, "save_verts", &varids[13]); ERR;
    err = ncmpi_get_vara_float_all(ncid, varids[13], start, count, 
				   v->save_verts); ERR;

    /* sites */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[9], start, count, 
				      (long long *)block_ofsts); ERR;
    v->sites = (float *)malloc(v->num_orig_particles * 3 * sizeof(float));
    start[0] = block_ofsts[start_block_ofst + b];
    start[1] = 0;
    count[0] = v->num_orig_particles;
    count[1] = 3;
    err = ncmpi_inq_varid(ncid, "sites", &varids[14]); ERR;
    err = ncmpi_get_vara_float_all(ncid, varids[14], start, count, 
				   v->sites); ERR;

    /* complete cells */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[6], start, count, 
				      (long long *) block_ofsts); ERR;
    v->complete_cells = (int *)malloc(v->num_complete_cells * sizeof(int));
    start[0] = block_ofsts[start_block_ofst + b];
    count[0] = v->num_complete_cells;
    err = ncmpi_inq_varid(ncid, "complete_cells", &varids[15]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[15], start, count, 
				 v->complete_cells); ERR;

    /* areas, uses same block offsets as complete cells */
    v->areas = (float *)malloc(v->num_complete_cells * sizeof(float));
    start[0] = block_ofsts[start_block_ofst + b];
    count[0] = v->num_complete_cells;
    err = ncmpi_inq_varid(ncid, "areas", &varids[16]); ERR;
    err = ncmpi_get_vara_float_all(ncid, varids[16], start, count, 
				   v->areas); ERR;

    /* volumes, uses same block offsets as complete cells */
    v->vols = (float *)malloc(v->num_complete_cells * sizeof(float));
    start[0] = block_ofsts[start_block_ofst + b];
    count[0] = v->num_complete_cells;
    err = ncmpi_inq_varid(ncid, "vols", &varids[17]); ERR;
    err = ncmpi_get_vara_float_all(ncid, varids[17], start, count, 
				   v->vols); ERR;

    /* neighbors */
    err = ncmpi_inq_varid(ncid, "neighbors", &varids[21]); ERR;
    err = ncmpi_inq_varid(ncid, "num_neighbors", &varids[24]); ERR;
    start[0] = start_block_ofst + b;
    count[0] = 1;
    err = ncmpi_get_vara_int_all(ncid, varids[24], start, count, 
				 &((*num_neighbors)[b])); ERR;
    if ((*num_neighbors)[b]) {
      (*neighbors)[b] = (int *)malloc((*num_neighbors)[b] * sizeof(int));
      (*neigh_procs)[b] = (int *)malloc((*num_neighbors)[b] * sizeof(int));
      start[0] = 0;
      count[0] = *tot_blocks;
      err = ncmpi_get_vara_longlong_all(ncid, varids[40], start, count, 
					(long long *) block_ofsts); ERR;
      start[0] = block_ofsts[start_block_ofst + b];
      count[0] = (*num_neighbors)[b];
      err = ncmpi_get_vara_int_all(ncid, varids[21], start, count, 
				   (*neighbors)[b]); ERR;
    }

    /* neigh_procs is not read from the file; rather distributed anew
       according to the number of processes being used now
       for now relies on blocks being in gid order in file */
    int n;
    for (n = 0; n < (*num_neighbors)[b]; n++) {
      (*neigh_procs)[b][n] = gid2idx[(*neighbors)[b][n]] / bp;
      if ((*neigh_procs)[b][n] >= groupsize)
	(*neigh_procs[b][n] = groupsize - 1);
    }

    /* gids */
    (*gids)[b] = all_gids[start_block_ofst + b];

    /* tets */
    /* local */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[26], start, count, 
				      (long long *) block_ofsts); ERR;
    v->loc_tets = (int *)malloc(4 * v->num_loc_tets * sizeof(int));
    count[0] = v->num_loc_tets;
    count[1] = (count[0] ? 4 : 0);
    start[0] = (count[0] ? block_ofsts[start_block_ofst + b] : 0);
    start[1] = 0;
    err = ncmpi_inq_varid(ncid, "loc_tets", &varids[27]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[27], start, count, 
				 v->loc_tets); ERR;
    /* remote */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[29], start, count, 
				      (long long *) block_ofsts); ERR;
    v->rem_tet_gids = (int *)malloc(4 * v->num_rem_tets * sizeof(int));
    v->rem_tet_nids = (int *)malloc(4 * v->num_rem_tets * sizeof(int));
    v->rem_tet_wrap_dirs = (unsigned char *)malloc(4 * v->num_rem_tets);
    count[0] = v->num_rem_tets;
    count[1] = (count[0] ? 4 : 0);
    start[0] = (count[0] ? block_ofsts[start_block_ofst + b] : 0);
    start[1] = 0;
    err = ncmpi_inq_varid(ncid, "rem_tet_gids", &varids[30]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[30], start, count, 
				 v->rem_tet_gids); ERR;
    err = ncmpi_inq_varid(ncid, "rem_tet_nids", &varids[31]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[31], start, count, 
				 v->rem_tet_nids); ERR;
    err = ncmpi_inq_varid(ncid, "rem_tet_wrap_dirs", &varids[32]); ERR;
    err = ncmpi_get_vara_uchar_all(ncid, varids[32], start, count, 
				   v->rem_tet_wrap_dirs); ERR;
    
    /* faces */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[35], start, count, 
				      (long long *) block_ofsts); ERR;
    v->faces = (struct vface_t *)malloc(v->num_faces * sizeof(struct vface_t));
    start[0] = block_ofsts[start_block_ofst + b];
    start[1] = 0;
    count[0] = v->num_faces;
    count[1] = 3 + MAX_FACE_VERTS;
    err = ncmpi_inq_varid(ncid, "faces", &varids[37]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[37], start, count, 
				 (int *)(v->faces)); ERR;

    /* cell_faces_start */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[9], start, count, 
				      (long long *) block_ofsts); ERR;
    v->cell_faces_start = (int *)malloc(v->num_orig_particles * sizeof(int));
    start[0] = block_ofsts[start_block_ofst + b];
    count[0] = v->num_orig_particles;
    err = ncmpi_inq_varid(ncid, "cell_faces_start", &varids[38]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[38], start, count, 
				 v->cell_faces_start); ERR;

    /* cell_faces */
    start[0] = 0;
    count[0] = *tot_blocks;
    err = ncmpi_get_vara_longlong_all(ncid, varids[7], start, count, 
				      (long long *) block_ofsts); ERR;
    v->cell_faces = (int *)malloc(v->tot_num_cell_faces * sizeof(int));
    start[0] = block_ofsts[start_block_ofst + b];
    count[0] = v->tot_num_cell_faces;
    err = ncmpi_inq_varid(ncid, "cell_faces", &varids[39]); ERR;
    err = ncmpi_get_vara_int_all(ncid, varids[39], start, count, 
				 v->cell_faces); ERR;

  } /* for all blocks */

  /* cleanup */
  err = ncmpi_close(ncid); ERR;
  free(block_ofsts);
  free(all_gids);
  free(gid2idx);

}
/*--------------------------------------------------------------------------*/
/*
  creates DIY datatype for the subset of the voronoi block to write to disk

  vblock: voronoi block
  did: domain id (unused in this case)
  lid: local block number (unused in this case)
  dtype: pointer to datatype

  side effects: commits datatype

*/
void create_datatype(void* vblock, int did, int lid, DIY_Datatype *dtype) {

  did = did; /* quiet compiler warning */
  lid = lid; 

  /* datatype for voronoi face */
  DIY_Datatype ftype;
  struct map_block_t face_map[] = {

    {DIY_INT,    OFST, 2,
     offsetof(struct vface_t, cells)               },
    {DIY_INT,    OFST, 1,
     offsetof(struct vface_t, num_verts)           },
    {DIY_INT,    OFST, MAX_FACE_VERTS,
     offsetof(struct vface_t, verts)               },

  };
  DIY_Create_struct_datatype(0, 3, face_map, &ftype);

  struct vblock_t *v = (struct vblock_t *)vblock;
  struct map_block_t map[] = {

    { DIY_FLOAT,  OFST, 3, 
      offsetof(struct vblock_t, mins)                 },
    { DIY_FLOAT, ADDR, v->num_verts * 3, 
      DIY_Addr(v->save_verts)                         },
    { DIY_FLOAT, ADDR, v->num_orig_particles * 3,
      DIY_Addr(v->sites)                              },
    { DIY_INT,    ADDR, v->num_complete_cells, 
      DIY_Addr(v->complete_cells)                     },
    { DIY_FLOAT, ADDR, v->num_complete_cells, 
      DIY_Addr(v->areas)                              },
    { DIY_FLOAT, ADDR, v->num_complete_cells, 
      DIY_Addr(v->vols)                               },
    { DIY_INT,    ADDR, v->num_loc_tets * 4, 
      DIY_Addr(v->loc_tets)                           },
    { DIY_INT,    ADDR, v->num_rem_tets * 4, 
      DIY_Addr(v->rem_tet_gids)                       },
    { DIY_INT,    ADDR, v->num_rem_tets * 4, 
      DIY_Addr(v->rem_tet_nids)                       },
    { DIY_BYTE,   ADDR, v->num_rem_tets * 4, 
      DIY_Addr(v->rem_tet_wrap_dirs)                  },
    { ftype,      ADDR, v->num_faces,
      DIY_Addr(v->faces)                              },
    { DIY_INT,    ADDR, v->num_orig_particles,
      DIY_Addr(v->cell_faces_start)                   },
    { DIY_INT,    ADDR, v->tot_num_cell_faces,
      DIY_Addr(v->cell_faces)                         },
    { DIY_FLOAT,  OFST, 3, 
      offsetof(struct vblock_t, maxs)                 },

  };

  DIY_Create_struct_datatype(DIY_Addr(vblock), 14, map, dtype);

  DIY_Destroy_datatype(&ftype);

}
/*--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------*/
/*
  writes output in diy format

  nblocks: number of blocks
  vblocks: pointer to array of vblocks
  hdrs: block headers
  out_file: output file name
*/
void diy_write(int nblocks, struct vblock_t *vblocks, int **hdrs, 
	       char *out_file) {

  int i;

  /* pointers to voronoi blocks, needed for writing output */
  void **pvblocks;
  pvblocks = (void**)malloc(sizeof(void*) * nblocks);
  for (i = 0; i < nblocks; i++)
    pvblocks[i] = &vblocks[i];

  /* write output */
  DIY_Write_open_all(0, out_file, 0); /* uncompressed for now */
  DIY_Write_blocks_all(0, pvblocks, nblocks, hdrs, &create_datatype);
  DIY_Write_close_all(0);

  free(pvblocks);

}
/*--------------------------------------------------------------------------*/
/*
  writes delaunay output in diy format

  nblocks: number of blocks
  dblocks: local delaunay blocks
  hdrs: block headers
  out_file: output file name
*/
void diy_dwrite(int nblocks, struct dblock_t *dblocks, int **hdrs, 
	       char *out_file) {

  int i;

  /* pointers to voronoi blocks, needed for writing output */
  void **pdblocks;
  pdblocks = (void**)malloc(sizeof(void*) * nblocks);
  for (i = 0; i < nblocks; i++)
    pdblocks[i] = &dblocks[i];

  /* write output */
  DIY_Write_open_all(0, out_file, 0); /* uncompressed for now */
  DIY_Write_blocks_all(0, pdblocks, nblocks, hdrs, &create_d_datatype);
  DIY_Write_close_all(0);

  free(pdblocks);

}
/*--------------------------------------------------------------------------*/