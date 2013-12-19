#include "tess.h"
#include "tess-cgal.h"
#include <vector>

/*--------------------------------------------------------------------------*/
/*
  creates local voronoi cells

  nblocks: number of blocks
  tblocks: pointer to array of temporary vblocks
  vblocks: pointer to array of vblocks
  dim: number of dimensions (eg. 3)
  num_particles: number of particles in each block
  particles: particles in each block, particles[block_num][particle]
  where each particle is 3 values, px, py, pz
  times: timing
*/
void local_cells(int nblocks, struct vblock_t *tblocks, 
		 struct vblock_t *vblocks, int dim,
		 int *num_particles, float **particles) {

  int i,j;

  /* for all blocks */
  for (i = 0; i < nblocks; i++) {
    Delaunay3D Dt;
    construct_delaunay(Dt, num_particles[i], particles[i]);

    /* process voronoi output */
    gen_voronoi_output(Dt, &tblocks[i], num_particles[i]);

    /* allocate cell sites for original particles */
    tblocks[i].num_orig_particles = num_particles[i];
    tblocks[i].sites =
      (float *)malloc(3 * sizeof(float) * tblocks[i].num_orig_particles);
    for (j = 0; j < tblocks[i].num_orig_particles; j++) {
      tblocks[i].sites[3 * j]     = particles[i][3 * j];
      tblocks[i].sites[3 * j + 1] = particles[i][3 * j + 1];
      tblocks[i].sites[3 * j + 2] = particles[i][3 * j + 2];
    }

    /* determine which cells are incomplete or too close to neighbor */
    incomplete_cells(&tblocks[i], &vblocks[i], i);

  } /* for all blocks */
}
/*--------------------------------------------------------------------------*/
/*
  creates original voronoi cells

  nblocks: number of blocks
  vblocks: pointer to array of vblocks
  dim: number of dimensions (eg. 3)
  num_particles: number of particles in each block
  num_orig_particles: number of original particles in each block, before any
  neighbor exchange
  particles: particles in each block, particles[block_num][particle]
  where each particle is 3 values, px, py, pz
  gids: global block ids of owners of received particles in each of my blocks
  nids: native particle ids of received particles in each of my blocks
  dirs: wrapping directions of received particles in each of my blocks
  times: timing
*/
void orig_cells(int nblocks, struct vblock_t *vblocks, int dim,
		int *num_particles, int *num_orig_particles, 
		float **particles, int **gids, int **nids, 
		unsigned char **dirs, double *times) {

  int num_recvd; /* number of received particles in current block */
  int i,j;

  /* for all blocks */
  for (i = 0; i < nblocks; i++) {

    /* number of received particles */
    num_recvd = num_particles[i] - num_orig_particles[i];

    Delaunay3D Dt;
    construct_delaunay(Dt, num_particles[i], particles[i]);

    /* process voronoi output */
    gen_voronoi_output(Dt, &vblocks[i], num_particles[i]);

    /* allocate cell sites for original particles */
    vblocks[i].num_orig_particles = num_orig_particles[i];
    vblocks[i].sites =
      (float *)malloc(3 * sizeof(float) * vblocks[i].num_orig_particles);
    for (j = 0; j < vblocks[i].num_orig_particles; j++) {
      vblocks[i].sites[3 * j]     = particles[i][3 * j];
      vblocks[i].sites[3 * j + 1] = particles[i][3 * j + 1];
      vblocks[i].sites[3 * j + 2] = particles[i][3 * j + 2];
    }

    /* allocate lookup table for cells completion status */
    vblocks[i].is_complete = 
      (unsigned char *)malloc(vblocks[i].num_orig_particles);

    /* determine complete cells */
    complete_cells(&vblocks[i], i);

    /* exchange complete cell status for exchanged particles */
#ifdef TIMING
    MPI_Barrier(comm);
    double t0 = MPI_Wtime();
#endif

    struct remote_ic_t **rics; /* is_complete status of received particles */
    rics = (struct remote_ic_t **)malloc(nblocks * 
					 sizeof(struct remote_ic_t *));
    neighbor_is_complete(nblocks, vblocks, rics);

#ifdef TIMING
    MPI_Barrier(comm);
    times[EXCH_TIME] += (MPI_Wtime() - t0);
#endif

    /* process delaunay output */
    gen_delaunay_output(Dt, &vblocks[i],
			gids[i], nids[i], dirs[i], rics[i], i,
			num_particles[i] - num_orig_particles[i]);

    /* cleanup */
    for (j = 0; j < nblocks; j++)
      free(rics[j]);
    free(rics);

    // TODO: surely this can be optimized
    /* connectivity of faces in voronoi cells */
    cell_faces(&vblocks[i]);

  } /* for all blocks */
}
/*--------------------------------------------------------------------------*/
/*
  generates voronoi output from CGAL

  Dt: CGAL's Delaunay3D structure
  vblock: pointer to one voronoi block, allocated by caller
  num_particles: number of particles used to generate the tessellation
  side effects: allocates data structures inside of vblock, caller's
  responsibility to free

  returns: number of cells found (<= original number of particles)
*/
int gen_voronoi_output(Delaunay3D &Dt, struct vblock_t *vblock, 
		       int num_particles) {

  int i,j;

  vblock->num_verts = Dt.number_of_finite_cells() + 1;
  int temp_num_cells = Dt.number_of_vertices();

  /* vertices */
  std::map<Cell_handle, int> tet_indices;	// TODO: perhaps, replace with something better
  vblock->verts = (double *)malloc(sizeof(double) * 3 * vblock->num_verts);
  vblock->verts[0] = vblock->verts[1] = vblock->verts[2] = std::numeric_limits<double>::infinity();
  i = 1; /* already did the infinity vertex, index 0 */
  for(Cell_iterator cit = Dt.finite_cells_begin(); cit != Dt.finite_cells_end(); ++cit)
  {
      Point center = cit->circumcenter(Dt.geom_traits());
      vblock->verts[3 * i]     = center.x();
      vblock->verts[3 * i + 1] = center.y();
      vblock->verts[3 * i + 2] = center.z();
      tet_indices[cit] = i;
      i++;
  }

  /*
     order Vertex_iterators in the order of original particles
     (CGAL switches the order of the points via a spatial sort)
  */
  std::vector< std::pair<unsigned, Vertex_handle> > vertices;
  for(Vertex_iterator vit = Dt.finite_vertices_begin(); vit != Dt.finite_vertices_end(); ++vit)
      vertices.push_back(std::make_pair(vit->info(), vit));
  std::sort(vertices.begin(), vertices.end());

  /* number of vertices in each cell; size is number of particles; 
     if a cell is skipped, the number of vertices will be 0*/
  vblock->num_cell_verts = (int *)malloc(sizeof(int) * num_particles);
  memset(vblock->num_cell_verts, 0, sizeof(int) * num_particles);
  int cell = 0; /* index of cell being processed */
  for(unsigned k = 0; k < vertices.size(); ++k)
  {
    Vertex_handle v = vertices[k].second;
    std::vector<Cell_handle> cell_vertices;	    // Delaunay cells are Voronoi vertices
    Dt.incident_cells(v, std::back_inserter(cell_vertices));

    int num_infinite = 0;
    for (j = 0; j < cell_vertices.size(); ++j)
      if (Dt.is_infinite(cell_vertices[j]))
	++num_infinite;
    vblock->num_cell_verts[cell] = cell_vertices.size();
    if (num_infinite > 1)
      vblock->num_cell_verts[cell] -= (num_infinite - 1);
    
    ++cell;
  }

  /* allocate the cell vertices */
  vblock->tot_num_cell_verts = 0;
/*   for (i = 0; i < temp_num_cells; i++) */
  for (i = 0; i < num_particles; i++)
    vblock->tot_num_cell_verts += vblock->num_cell_verts[i];
  vblock->cells = (int *)malloc(sizeof(int) * vblock->tot_num_cell_verts);

  /* cell vertices */
  i = 0;
  for(unsigned k = 0; k < vertices.size(); ++k)
  {
    Vertex_handle v = vertices[k].second;
    std::vector<Cell_handle> cell_vertices;	    // Delaunay cells are Voronoi vertices
    Dt.incident_cells(v, std::back_inserter(cell_vertices));

    bool seen_infinite = false;
    for (j = 0; j < cell_vertices.size(); ++j)
    {
      if (Dt.is_infinite(cell_vertices[j]))
      {
	if (!seen_infinite)
	{
	  vblock->cells[i++] = 0;
	  seen_infinite = true;
	}
      } else
        vblock->cells[i++] = tet_indices[cell_vertices[j]];
    }
  }

  /* voronoi faces */
  int tot_faces = Dt.number_of_finite_edges();
  vblock->faces = (struct vface_t*)malloc(tot_faces * sizeof(struct vface_t));
  memset(vblock->faces, 0, tot_faces * sizeof(struct vface_t));
  int num_faces = 0;

  for(Edge_iterator eit = Dt.finite_edges_begin(); eit != Dt.finite_edges_end(); ++eit)
  {
    Cell_handle c = eit->first;
    Vertex_handle v0 = c->vertex(eit->second);
    Vertex_handle v1 = c->vertex(eit->third);
    vblock->faces[num_faces].cells[0]  = v0->info();
    vblock->faces[num_faces].cells[1]  = v1->info();

    int num_verts = 0;
    Cell_circulator begin = Dt.incident_cells(*eit);
    Cell_circulator cur = begin;
    bool seen_infinite = false;
    do
    {
      if (Dt.is_infinite(cur))
      {
	if (!seen_infinite)
	{
	  vblock->faces[num_faces].verts[num_verts++] = 0;
	  seen_infinite = true;
	}
      } else
	vblock->faces[num_faces].verts[num_verts++] = tet_indices[cur];
      ++cur;
    } while (cur != begin);
    vblock->faces[num_faces].num_verts = num_verts;
    ++num_faces;
  }

  vblock->num_faces = num_faces;
  assert(vblock->num_faces == tot_faces); /* sanity */

  return temp_num_cells;

}
/*--------------------------------------------------------------------------*/
/*
  generates delaunay output from qhull

  facetlist: qhull list of convex hull facets
  vblock: pointer to one voronoi block, allocated by caller
  num_particles: number of input particles (voronoi sites) both complete and
  incomplete, including any ghosted particles
  gids: global block ids of owners of received particles
  nids: native particle ids of received particles
  dirs: wrap directions of received particles
  rics: completion status of received particles
  num_recvd: number of received particles, number of gids and nids
  lid: current block local id
  num_recvd: number of remote particles received
  side effects: allocates data structures inside of vblock, caller's
  responsibility to free

  returns: number of tets found
*/
int gen_delaunay_output(Delaunay3D &Dt, struct vblock_t *vblock,
			int *gids, int *nids, unsigned char *dirs,
			struct remote_ic_t *rics, int lid, int num_recvd) {

  int n = 0; /* number of vertices in strictly local final tets */
  int m = 0; /* number of vertices in non strictly local final tets */
  int tet_verts[4]; /* current tet verts */

  int numfacets = Dt.number_of_finite_cells();
  vblock->loc_tets = (int *)malloc(numfacets * 4 * sizeof(int));
  vblock->rem_tet_gids = (int *)malloc(numfacets * 4 * sizeof(int));
  vblock->rem_tet_nids = (int *)malloc(numfacets * 4 * sizeof(int));
  vblock->rem_tet_wrap_dirs = (unsigned char *)malloc(numfacets * 4);

  // process the tets
  for(Cell_iterator cit = Dt.finite_cells_begin(); cit != Dt.finite_cells_end(); ++cit)
  {
    for (int i = 0; i < 4; ++i)
      tet_verts[i] = cit->vertex(i)->info();

    gen_delaunay_tet(tet_verts, vblock, gids, nids, dirs, rics, lid, num_recvd, &n, &m);
  }
  
  vblock->num_loc_tets = n / 4;
  vblock->num_rem_tets = m / 4;

  return (vblock->num_loc_tets + vblock->num_rem_tets);
}
/*--------------------------------------------------------------------------*/
/*
   compute Delaunay

   We should be inserting points as a batch (it would be much faster).
   Unfortunately, when adding info to Delaunay points (as we do), there seems
   to be a bug in CGAL's spatial sorting routines. I've reported it to the CGAL
   mailing list.
*/
void construct_delaunay(Delaunay3D &Dt, int num_particles, float *particles)
{
    std::vector< std::pair<Point,unsigned> > points; points.reserve(num_particles);
    for (unsigned j = 0; j < num_particles; j++)
    {
      Point p(particles[3*j],
	      particles[3*j+1],
	      particles[3*j+2]);
      points.push_back(std::make_pair(p, j));
      Dt.insert(p)->info() = j;
    }
}