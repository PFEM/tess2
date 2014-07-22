#include "mpi.h"
#include <assert.h>
#include <string.h>
#include "tess/tess.h"
#include <diy/mpi.hpp>

void GetArgs(int argc, char **argv, int &tb, int &mb, int *dsize, float *jitter,
	     float *minvol, float *maxvol, int *wrap, int *walls, char *outfile);

int main(int argc, char *argv[])
{
  int tb; // total number of blocks in the domain
  int mb; // max blocks in memory
  int dsize[3]; // domain grid size
  float jitter; // max amount to randomly displace particles
  float minvol, maxvol; // volume range, -1.0 = unused
  double times[MAX_TIMES]; // timing
  int wrap; // wraparound neighbors flag
  int walls; // apply walls to simulation (wrap must be off)
  char outfile[256]; // output file name

  // init MPI
  MPI_Comm comm = MPI_COMM_WORLD;
  MPI_Init(&argc, &argv);

  GetArgs(argc, argv, tb, mb, dsize, &jitter, &minvol, &maxvol, &wrap, &walls, outfile);

  tess_test(tb, mb, dsize, jitter, minvol, maxvol, wrap, walls, times, outfile, comm);

  MPI_Finalize();

  return 0;

}
//----------------------------------------------------------------------------
//
// gets command line args
//
void GetArgs(int argc, char **argv, int &tb, int &mb, int *dsize, float *jitter,
	     float *minvol, float *maxvol, int *wrap, int *walls, char *outfile)
{
  assert(argc >= 11);

  tb = atoi(argv[1]);
  mb = atoi(argv[2]);
  dsize[0] = atoi(argv[3]);
  dsize[1] = atoi(argv[4]);
  dsize[2] = atoi(argv[5]);
  *jitter = atof(argv[6]);
  *minvol = atof(argv[7]);
  *maxvol = atof(argv[8]);
  *wrap = atoi(argv[9]);
  *walls = atoi(argv[10]);
  if (argv[11][0] =='!')
    strcpy(outfile, "");
  else
    strcpy(outfile, argv[11]);
}
//----------------------------------------------------------------------------
