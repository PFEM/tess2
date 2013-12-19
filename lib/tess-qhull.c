#include "tess.h"
#include "tess-qhull.h"

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

  boolT ismalloc = False;    /* True if qhull should free points in
				qh_freeqhull() or reallocation */
  char flags[250];          /* option flags for qhull, see qh-quick.htm */
  int exitcode;             /* 0 if no error from qhull */
  int curlong, totlong;     /* memory remaining after qh_memfreeshort */
  FILE *dev_null; /* file descriptor for writing to /dev/null */
  int i, j;

  dev_null = fopen("/dev/null", "w");
  assert(dev_null != NULL);

  /* for all blocks */
  for (i = 0; i < nblocks; i++) {

    /* deep copy from float to double (qhull API is double) */
    double *pts = (double *)malloc(num_particles[i] * 3 * sizeof(double));
    for (j = 0; j < 3 * num_particles[i]; j++)
      pts[j] = particles[i][j];

    /* compute voronoi */
/*     sprintf(flags, "qhull v o Fo"); /\* print voronoi cells *\/ */
    sprintf (flags, "qhull d Qt"); /* print delaunay cells */

    /* eat qhull output by sending it to dev/null
       need to see how this behaves on BG/P, will get I/O forwarded but will 
       stop there and not proceed to storage */
    exitcode = qh_new_qhull(dim, num_particles[i], pts, ismalloc,
			    flags, dev_null, stderr);

    free(pts);

    /* process voronoi output */
    if (!exitcode)
      gen_voronoi_output(qh facet_list, &tblocks[i], num_particles[i]);

    /* allocate cell sites for original particles */
    tblocks[i].num_orig_particles = num_particles[i];
    tblocks[i].sites =
      (float *)malloc(3 * sizeof(float) * tblocks[i].num_orig_particles);
    for (j = 0; j < tblocks[i].num_orig_particles; j++) {
      tblocks[i].sites[3 * j] = particles[i][3 * j];
      tblocks[i].sites[3 * j + 1] = particles[i][3 * j + 1];
      tblocks[i].sites[3 * j + 2] = particles[i][3 * j + 2];
    }

    /* determine which cells are incomplete or too close to neighbor */
    incomplete_cells(&tblocks[i], &vblocks[i], i);

    /* clean up qhull */
    qh_freeqhull(!qh_ALL);                 /* free long memory */
    qh_memfreeshort(&curlong, &totlong);  /* free short memory */
    if (curlong || totlong)
      fprintf (stderr, "qhull internal warning: did not free %d bytes of "
	       "long memory (%d pieces)\n", totlong, curlong);

  } /* for all blocks */

  fclose(dev_null);

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

  boolT ismalloc = False;    /* True if qhull should free points in
				qh_freeqhull() or reallocation */
  char flags[250];          /* option flags for qhull, see qh-quick.htm */
  int exitcode;             /* 0 if no error from qhull */
  int curlong, totlong;     /* memory remaining after qh_memfreeshort */
  FILE *dev_null; /* file descriptor for writing to /dev/null */
  int num_recvd; /* number of received particles in current block */
  int i, j;

  dev_null = fopen("/dev/null", "w");
  assert(dev_null != NULL);

  /* for all blocks */
  for (i = 0; i < nblocks; i++) {

    /* number of received particles */
    num_recvd = num_particles[i] - num_orig_particles[i];

    /* deep copy from float to double (qhull API is double) */
    double *pts = (double *)malloc(num_particles[i] * 3 * sizeof(double));
    for (j = 0; j < 3 * num_particles[i]; j++)
      pts[j] = particles[i][j];

    /* compute voronoi */
/*     sprintf(flags, "qhull v o Fv Fo"); /\* Fv prints voronoi faces *\/ */
/*     sprintf(flags, "qhull d Qt"); /\* print delaunay cells *\/ */
    sprintf(flags, "qhull v d Fv Qt"); /* Fv prints voronoi faces */

    /* eat qhull output by sending it to dev/null
       need to see how this behaves on BG/P, will get I/O forwarded but will 
       stop there and not proceed to storage */
    exitcode = qh_new_qhull(dim, num_particles[i], pts, ismalloc,
			    flags, dev_null, stderr);

    free(pts);

    /* process voronoi output */
    if (!exitcode)
      gen_voronoi_output(qh facet_list, &vblocks[i], num_particles[i]);

    /* allocate cell sites for original particles */
    vblocks[i].num_orig_particles = num_orig_particles[i];
    vblocks[i].sites =
      (float *)malloc(3 * sizeof(float) * vblocks[i].num_orig_particles);
    for (j = 0; j < vblocks[i].num_orig_particles; j++) {
      vblocks[i].sites[3 * j] = particles[i][3 * j];
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
    if (!exitcode)
      gen_delaunay_output(qh facet_list, &vblocks[i],
			  gids[i], nids[i], dirs[i], rics[i], i,
			  num_particles[i] - num_orig_particles[i]);

    /* cleanup */
    for (j = 0; j < nblocks; j++)
      free(rics[j]);
    free(rics);

    /* clean up qhull */
    qh_freeqhull(!qh_ALL);                 /* free long memory */
    qh_memfreeshort(&curlong, &totlong);  /* free short memory */
    if (curlong || totlong)
      fprintf (stderr, "qhull internal warning: did not free %d bytes of "
	       "long memory (%d pieces)\n", totlong, curlong);

    /* connectivity of faces in voronoi cells */
    cell_faces(&vblocks[i]);

  } /* for all blocks */

  fclose(dev_null);

}
/*--------------------------------------------------------------------------*/
/*
  generates voronoi output from qhull

  facetlist: qhull list of convex hull facets
  vblock: pointer to one voronoi block, allocated by caller
  num_particles: number of particles used to generate the tessellation
  side effects: allocates data structures inside of vblock, caller's
  responsibility to free

  returns: number of cells found (<= original number of particles)
*/
int gen_voronoi_output(facetT *facetlist, struct vblock_t *vblock, 
		       int num_particles) {

  int i, numcenters, numvertices= 0, numneighbors, numinf, vertex_i, vertex_n;
  facetT *facet, *neighbor, **neighborp;
  setT *vertices;
  vertexT *vertex, **vertexp;
  boolT isLower;
  unsigned int numfacets= (unsigned int) qh num_facets;
  int *skip_cells = NULL; /* cells (input particles) skipped by qhull */
  int num_skip_cells = 0; /* number of skipped cells */
  int alloc_skip_cells = 0; /* allocated number of skipped cells */
  int chunk_size = 1024; /* chunk size for allocating skip_cells */

  /* init, get counts */
  int cell = 0; /* index of cell being processed */
  vertices = qh_markvoronoi(facetlist, NULL, 0, &isLower, &numcenters);
  FOREACHvertex_i_(vertices) {
    if (vertex) {
      numvertices++;
      numneighbors = numinf = 0;
      FOREACHneighbor_(vertex) {
        if (neighbor->visitid == 0)
          numinf= 1;
        else if (neighbor->visitid < numfacets)
          numneighbors++;
      }
      if (numinf && !numneighbors) {
        SETelem_(vertices, vertex_i)= NULL;
        numvertices--;
	add_int(cell, &skip_cells, &num_skip_cells, &alloc_skip_cells,
		chunk_size);
      }
    }
    else
	add_int(cell, &skip_cells, &num_skip_cells, &alloc_skip_cells,
		chunk_size);
    cell++;
  }

  /* number of verts and cells may appear to be reversed, but this is
     qhull's nomenclature (makes sense for delaunay) and is actually correct
  */
  vblock->num_verts = numcenters;
  int temp_num_cells = numvertices;

  /* vertices */
  vblock->verts = (double *)malloc(sizeof(double) * 3 * vblock->num_verts);

  vblock->verts[0] = vblock->verts[1] = vblock->verts[2] = qh_INFINITE;
  i = 1; /* already did the infinity vertex, index 0 */
  FORALLfacet_(facetlist) {
    if (facet->visitid && facet->visitid < numfacets) {
      if (!facet->center)
	facet->center = qh_facetcenter(facet->vertices);
      vblock->verts[3 * i] = facet->center[0];
      vblock->verts[3 * i + 1] = facet->center[1];
      vblock->verts[3 * i + 2] = facet->center[2];
      i++;
    }
  }

  /* number of vertices in each cell; size is number of particles; 
     if a cell is skipped, the number of vertices will be 0*/
  vblock->num_cell_verts = (int *)malloc(sizeof(int) * num_particles);
  memset(vblock->num_cell_verts, 0, sizeof(int) * num_particles);
  cell = 0;
  FOREACHvertex_i_(vertices) {
    numneighbors = 0;
    numinf = 0;
    if (vertex) {
      FOREACHneighbor_(vertex) {
        if (neighbor->visitid == 0)
          numinf= 1;
        else if (neighbor->visitid < numfacets)
          numneighbors++;
      }
      if (numinf)
	numneighbors++;
      vblock->num_cell_verts[cell++] = numneighbors;
    }
    else
      cell++;
  }

  /* allocate the cell vertices */
  vblock->tot_num_cell_verts = 0;
/*   for (i = 0; i < temp_num_cells; i++) */
  for (i = 0; i < num_particles; i++)
    vblock->tot_num_cell_verts += vblock->num_cell_verts[i];
  vblock->cells = (int *)malloc(sizeof(int) * vblock->tot_num_cell_verts);

  /* cell vertices */
  i = 0;
  FOREACHvertex_i_(vertices) {
    if (vertex) {
      numinf = 0;
      FOREACHneighbor_(vertex) {
	if (neighbor->visitid < numfacets) {
	  if (!numinf || neighbor->visitid > 0) {
	    vblock->cells[i++] = neighbor->visitid;
	    if (neighbor->visitid == 0)
	      numinf++;
	  }
	}
	else if (numinf && neighbor->visitid < numfacets)
	  vblock->cells[i++] = neighbor->visitid;
      }
    }
  }

  /* voronoi faces */
  int tot_faces = qh_printvdiagram2 (NULL, NULL, vertices, qh_RIDGEall, False);
  vblock->faces = (struct vface_t*)malloc(tot_faces * sizeof(struct vface_t));
  memset(vblock->faces, 0, tot_faces * sizeof(struct vface_t));
  int num_faces = 0;

  FORALLvertices
    vertex->seen= False;
  FOREACHvertex_i_(vertices) {
    if (vertex) {
      if (qh GOODvertex > 0 && qh_pointid(vertex->point)+1 != qh GOODvertex)
        continue;

      /* following is equivalent to calling
	 qh_eachvoronoi(stderr, qh_printvridge, vertex, !qh_ALL, qh_RIDGEall,
	 True); */

      vertexT *atvertex = vertex;
      boolT unbounded;
      int count;
      facetT *neighbor, **neighborp, *neighborA, **neighborAp;
      setT *centers;
      setT *tricenters= qh_settemp(qh TEMPsize);
      boolT firstinf;
      unsigned int numfacets= (unsigned int)qh num_facets;
      int totridges= 0;

      qh vertex_visit++;
      atvertex->seen= True;
      FOREACHneighbor_(atvertex) {
	if (neighbor->visitid < numfacets)
	  neighbor->seen= True;
      }
      FOREACHneighbor_(atvertex) {
	if (neighbor->seen) {

	  FOREACHvertex_(neighbor->vertices) {

	    if (vertex->visitid != qh vertex_visit && !vertex->seen) {
	      vertex->visitid= qh vertex_visit;
	      count= 0;
	      firstinf= True;
	      qh_settruncate(tricenters, 0);

	      FOREACHneighborA_(vertex) {
		if (neighborA->seen) {
		  if (neighborA->visitid) {
		    if (!neighborA->tricoplanar ||
			qh_setunique(&tricenters, neighborA->center))
		      count++;
		  }else if (firstinf) {
		    count++;
		    firstinf= False;
		  }
		}
	      } /* FOREACHneighborA */

	      if (count >= qh hull_dim - 1) {  /* e.g., 3 for 3-d Voronoi */
		if (firstinf)
		  unbounded= False;
		else
		  unbounded= True;
		totridges++;
		trace4((qh ferr, 4017,
			"qh_eachvoronoi: Voronoi ridge of %d vertices "
			"between sites %d and %d\n",
			count, qh_pointid(atvertex->point),
			qh_pointid(vertex->point)));

		if (qh hull_dim == 3+1) /* 3-d Voronoi diagram */
		  centers= qh_detvridge3 (atvertex, vertex);
		else
		  centers= qh_detvridge(vertex);

		/* following is equivvalent to calling
		   qh_printvridge(fp, atvertex, vertex, centers, unbounded) */

		facetT *facet, **facetp;
		QHULL_UNUSED(unbounded);
		vblock->faces[num_faces].num_verts = qh_setsize(centers);
		vblock->faces[num_faces].cells[0] = qh_pointid(atvertex->point);
		vblock->faces[num_faces].cells[1] = qh_pointid(vertex->point);
		int num_verts = 0;
		FOREACHfacet_(centers) {
		  assert(num_verts < MAX_FACE_VERTS);
		  vblock->faces[num_faces].verts[num_verts++] = facet->visitid;
		}
		num_faces++;

		/* end of equivalence of calling qh_printvridge */

		qh_settempfree(&centers);

	      } /* if count >= ... */

	    } /* if (vertex->visitid ... */

	  } /* FOREACHvertex */

	} /* if (neighbor_seen) */

      } /* FOREACHneighbor */

      FOREACHneighbor_(atvertex)
	neighbor->seen= False;
      qh_settempfree(&tricenters);

      /* end of equivalence of calling qh_eachvoronoi() */

    } /* if (vertex) */

  } /* FOREACHvertex */

  vblock->num_faces = num_faces;
  assert(vblock->num_faces == tot_faces); /* sanity */

  /* clean up */
  qh_settempfree(&vertices);
  if (skip_cells)
    free(skip_cells);

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
int gen_delaunay_output(facetT *facetlist, struct vblock_t *vblock,
			int *gids, int *nids, unsigned char *dirs,
			struct remote_ic_t *rics, int lid, int num_recvd) {

  facetT *facet;
  vertexT *vertex, **vertexp;
  int numfacets = 0;
  int n = 0; /* number of vertices in strictly local final tets */
  int m = 0; /* number of vertices in non strictly local final tets */
  int v; /* vertex in current tet (0, 1, 2, 3) */
  int tet_verts[4]; /* current tet verts */
  int i;

  /* count number of facets */
  FORALLfacet_(facetlist) {
    if ((facet->visible && qh NEWfacets) || (qh_skipfacet(facet)))
      facet->visitid= 0;
    else
      facet->visitid= ++numfacets;
  }

  /* qhull lifts the 3d problem to a 4d convex hull
     therefore, its definition of a facet (dim - 1) is exactly our
     definition of a tet */
  /* todo: static allocation wasteful; we don't know how many tets
     are local and how many are remote; use add_int to groew arrays instead */
  vblock->loc_tets = (int *)malloc(numfacets * 4 * sizeof(int));
  vblock->rem_tet_gids = (int *)malloc(numfacets * 4 * sizeof(int));
  vblock->rem_tet_nids = (int *)malloc(numfacets * 4 * sizeof(int));
  vblock->rem_tet_wrap_dirs = (unsigned char *)malloc(numfacets * 4);

  /* for all tets (facets to qhull) */
  FORALLfacet_(facetlist) {

    if (qh_skipfacet(facet) || (facet->visible && qh NEWfacets))
      continue;

    if (qh_setsize(facet->vertices) != 4) {
      fprintf(stderr, "tet %d has %d vertices; skipping.\n", n / 4,
	      qh_setsize(facet->vertices));
      continue;
    }

    if ((facet->toporient ^ qh_ORIENTclock)
	|| (qh hull_dim > 2 && !facet->simplicial)) {
      v = 0; /* vertex in tet (0, 1, 2, 3) */
      FOREACHvertex_(facet->vertices)
	tet_verts[v++] = qh_pointid(vertex->point);
    } else {
      v = 0;
      FOREACHvertexreverse12_(facet->vertices)
	tet_verts[v++] = qh_pointid(vertex->point);
    }

    gen_delaunay_tet(tet_verts, vblock, gids, nids, dirs, rics, lid, num_recvd, &n, &m);

  } /* for all tets */

  /* adjust num_tets in case any facets were skipped */
  vblock->num_loc_tets = n / 4;
  vblock->num_rem_tets = m / 4;

  return (vblock->num_loc_tets + vblock->num_rem_tets);

}