/*
 * AVI.h
 * DG++
 *
 * Created by Mark Potts on 3/25/09.
 *  
 * Copyright (c) 2009 Adrian Lew
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including 
 * without limitation the rights to use, copy, modify, merge, publish, 
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included 
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "StandardAVI.h"

#include "util.h"



bool StandardAVI::gather ( const LocalToGlobalMap& L2G, const VecDouble& Qval, const VecDouble& Vval,
    const VecDouble& Vbval, const VecDouble& Tval,
    MatDouble& q,
    MatDouble& v,
    MatDouble& vb,
    MatDouble& ti) const {

  // double *Q, *V, *Vb, *T;
// 
  // VecGetArray (Qval,& Q);
  // VecGetArray (Vval,& V);
  // VecGetArray (Vbval,& Vb);
  // VecGetArray (Tval,& T);


  if (q.size () < nfields)
    q.resize (nfields);
  if (v.size () < nfields)
    v.resize (nfields);
  if (vb.size () < nfields)
    vb.resize (nfields);
  if (ti.size () < nfields)
    ti.resize (nfields);

  for (size_t f = 0; f < nfields; f++) {
    if (q[f].size () < getFieldDof (f))
      q[f].resize (getFieldDof (f));

    if (v[f].size () < getFieldDof (f))
      v[f].resize (getFieldDof (f));

    if (vb[f].size () < getFieldDof (f))
      vb[f].resize (getFieldDof (f));

    if (ti[f].size () < getFieldDof (f))
      ti[f].resize (getFieldDof (f));

    for (size_t a = 0; a < getFieldDof (f); a++) {
      size_t index = L2G.map (f, a, this->globalIdx);
      q[f][a] = Qval[index];
      v[f][a] = Vval[index];
      vb[f][a] = Vbval[index];
      ti[f][a] = Tval[index];
    }
  }

  // VecRestoreArray (Qval,& Q);
  // VecRestoreArray (Vval,& V);
  // VecRestoreArray (Vbval,& Vb);
  // VecRestoreArray (Tval,& T);


  return (true);
}
;

void StandardAVI::computeLocalTvec (MatDouble& tnew) const {
  assert (tnew.size () == nfields);

  for (size_t f = 0; f < nfields; f++) {
    size_t fieldDof = getFieldDof (f);
    assert (tnew[f].size () == fieldDof);

    for (size_t a = 0; a < fieldDof; a++) {
      tnew[f][a] = getNextTimeStamp ();
    }
  }
}


bool StandardAVI::assemble (const LocalToGlobalMap& L2G, 
    const MatDouble& qnew,
    const MatDouble& vnew,
    const MatDouble& vbnew, 
    const MatDouble& tnew,
    VecDouble& Qval, VecDouble& Vval,
    VecDouble& Vbval, VecDouble& Tval, VecDouble& LUpdate) const {

  // double * resvals = new double[localsize];
  // double * vvals = new double[localsize];
  // double * vbvals = new double[localsize];
  // double * tvals = new double[localsize];
 
  // size_t * indices = new size_t[localsize];
  // double * updates = new double[localsize];


  for (size_t f = 0, i = 0; f < nfields; f++) {
    for (size_t a = 0; a < getFieldDof (f); a++, i++) {
      size_t index = L2G.map (f, a, globalIdx);
      Qval[index] = qnew[f][a];
      Vval[index] = vnew[f][a];
      Vbval[index] = vbnew[f][a];
      Tval[index] = tnew[f][a];
      LUpdate[index] = (double) globalIdx;
    }
  }

  // VecSetValues (*Qval, localsize, indices, resvals, INSERT_VALUES);
  // VecSetValues (*Vval, localsize, indices, vvals, INSERT_VALUES);
  // VecSetValues (*Vbval, localsize, indices, vbvals, INSERT_VALUES);
  // VecSetValues (*Tval, localsize, indices, tvals, INSERT_VALUES);
  // VecSetValues (*LUpdate, localsize, indices, updates, INSERT_VALUES);

  // delete[] resvals;
  // delete[] vvals;
  // delete[] vbvals;
  // delete[] tvals;
  // delete[] indices;
  // delete[] updates;

  return (true);
}
;

bool StandardAVI::update (const MatDouble& q,
    const MatDouble& v,
    const MatDouble& vb,
    const MatDouble& ti,
    const MatDouble& tnew,
    MatDouble& qnew,
    MatDouble& vnew,
    MatDouble& vbnew,
    MatDouble& forcefield,
    MatDouble& funcval,
    MatDouble& deltaV
    ) const {

  for (size_t f = 0; f < nfields; f++) {
    for (size_t a = 0; a < getFieldDof (f); a++) {
#if 0
      if(imposedFlags[f][a] == true) {
        qnew[f][a]=imposedValues[f][a];
      }
      else {
        qnew[f][a]=q[f][a] + (getNextTimeStamp() - ti[f][a]) * vb[f][a];
      }
#else
      if (imposedTypes[f][a] == ONE) {
        double t = ti[f][a];
        qnew[f][a] = ((avi_bc_func)[a]) (f, a, t);
      }
      else {
        qnew[f][a] = q[f][a] + (tnew[f][a] - ti[f][a]) * vb[f][a];
      }
#endif
    }
  }

  // MatDouble funcval (nfields);
  getForceField (qnew, forcefield);

  assert (funcval.size () == nfields);

  for (size_t f = 0; f < nfields; f++) {

    assert (funcval[f].size () == getFieldDof (f));

    for (size_t a = 0; a < getFieldDof (f); a++) {
      funcval[f][a] = -(getTimeStep ()) * (forcefield)[f][a];
    }
  }

  // MatDouble DeltaV;
  computeDeltaV (funcval, deltaV);

  for (size_t f = 0; f < nfields; f++) {
    for (size_t a = 0; a < getFieldDof (f); a++) {
      if (imposedTypes[f][a] == ZERO) {
        vnew[f][a] = vb[f][a] + deltaV[f][a] / 2.0;
        vbnew[f][a] = vb[f][a] + deltaV[f][a];
      }
      else if (imposedTypes[f][a] == ONE) {
        vnew[f][a] = 0.0;
        vbnew[f][a] = 0.0;
      }
      else if (imposedTypes[f][a] == TWO) {
        double t = ti[f][a];
        vnew[f][a] = ((avi_bc_func)[a]) (f, a, t);
        vbnew[f][a] = ((avi_bc_func)[a]) (f, a, t);
      }
    }
  }

  // XXX (amber) Commented, must make explicit call to incTimeStamp in main loop
  // setTimeStamp (getNextTimeStamp ());

  return (true);
}
;

//! Initialization routine to set values at the half time step
//! This function is called only once per element and is called after gather
//! and before update and assemble.
//! @param q used to calculate the velocity at the half step.
//! local vector with the last known values of the degrees of freedom, \f$q_a^{i-1}\f$ in
//! the notation above.  \n
//! @param vb Upon exit, local vector with the initial values of the time derivatives at the
//! middle of a time step
//! of the degrees of freedom,  \f$v_a^{i-1/2}\f$ in the notation above.\n
//! @param ti local vector with the last update time of a the degrees of freedom,
//! \f$t_a^{i-1}\f$ in the notation above.\n
//! @param ElementIndex GlobalElementIndex or index of the force field in the AVI object, used to
//! identify its global degrees of freedom. This information is not embedded in the object. \n
//! @param L2G Local to Global map used to find the values in the Global arrays\n
//! @param imposedFlags Upon exit, local vector with true when a degree of freedom has its values
//! imposed. \n
//! @param imposedValues Upon exit, local vector with value to impose on a degree of freedom, if its
//! value should be imposed. \n
//!
//! For example, q[f][b] = Q[L2G.map(ElementIndex,getFields()[f],b)]
//!
//! This function uses getImposedValues to fill the imposed values arrays.

bool StandardAVI::vbInit (
    const MatDouble& q,
    const MatDouble& v,
    const MatDouble& vb,
    const MatDouble& ti,
    const MatDouble& tnew,
    MatDouble& qnew,
    MatDouble& vbinit,
    MatDouble& forcefield,
    MatDouble& funcval,
    MatDouble& deltaV
    ) const {

  // MatDouble qnew;

  // qnew.resize ((q).size ());

  assert (qnew.size () == q.size ());

  for (size_t f = 0; f < nfields; f++) {

    assert (qnew[f].size () == q[f].size ());

    for (size_t a = 0; a < getFieldDof (f); a++) {
      if (imposedFlags[f][a] == true) {
        qnew[f][a] = imposedValues[f][a];
      }
      else {
        qnew[f][a] =  (q)[f][a] + (tnew[f][a] - ti[f][a]) / 2.0 * vb[f][a];
      }
    }
  }

  getForceField ((qnew), forcefield);

#ifdef DEBUG
  std::cerr << "forcefield = ";
  for (size_t f = 0; f < nfields; ++f) {
    printIter(std::cerr, forcefield[f].begin(), forcefield[f].end());
  }
#endif

  // MatDouble funcval (nfields);
  assert (funcval.size () == nfields);

  for (size_t f = 0; f < nfields; f++) {

    assert (funcval[f].size () == getFieldDof (f));
    for (size_t a = 0; a < getFieldDof (f); a++) {
      funcval[f][a] = -(getTimeStep ()) * (forcefield)[f][a];
    }
  }

  // MatDouble DeltaV;
  computeDeltaV (funcval, deltaV);

#ifdef DEBUG
  std::cerr << "funcval = ";
  for (size_t f = 0; f < nfields; ++f) {
    printIter(std::cerr, funcval[f].begin(), funcval[f].end());
  }
  std::cerr << "DeltaV = ";
  for (size_t f = 0; f < nfields; ++f) {
    printIter(std::cerr, DeltaV[f].begin(), DeltaV[f].end());
  }
#endif

  for (size_t f = 0; f < nfields; f++) {
    for (size_t a = 0; a < getFieldDof (f); a++) {
      vbinit[f][a] = (vb[f][a] + deltaV[f][a] / 2.0);
    }
  }
  return (true);
}
;




void StandardAVI::computeDeltaV(const MatDouble& funcval,
    MatDouble& DeltaV) const {

  if(DeltaV.size()<nfields) {
    DeltaV.resize(nfields);
  }

  for(size_t f=0;f<nfields;f++) {
    if(DeltaV[f].size()<getFieldDof(f)) {
      DeltaV[f].resize(getFieldDof(f));
    } 
    for(size_t a=0;a<getFieldDof(f);a++) {
      DeltaV[f][a] = funcval[f][a]/(MMdiag[f][a]);
    }
  }
};

//! \todo -- Test this function.  The velocity values, in particular, need to be fixed
bool StandardAVI::getImposedValues (const GlobalElementIndex& ElementIndex,
    const LocalToGlobalMap& L2G, size_t field, size_t dof,
    double& qvalue, double& vvalue) const {

  if (imposedFlags[field][dof] == true) {
    qvalue = imposedValues[field][dof];
    vvalue = 0.0; //this needs to be re-worked
    return (true);
  }
  else {
    return (false);
  }
}
;

void StandardAVI::setBCs (const MatBool& IFlag,
    const MatDouble& IVal) {

  if (imposedFlags.size () != IFlag.size ()) {
    imposedFlags.resize (IFlag.size ());
  }

  if (imposedValues.size () != IVal.size ()) {
    imposedValues.resize (IVal.size ());
  }

  for (size_t f = 0; f < IFlag.size (); f++) {
    if (imposedFlags[f].size () != IFlag[f].size ()) {
      imposedFlags[f].resize (IFlag[f].size ());
    }
    if (imposedValues[f].size () != IVal[f].size ()) {
      imposedValues[f].resize (IVal[f].size ());
    }
    for (size_t a = 0; a < IFlag[f].size (); a++) {
      imposedValues[f][a] = IVal[f][a];
      imposedFlags[f][a] = IFlag[f][a];
    }
  }
}
;

void StandardAVI::setDiagVals (const VecDouble& MassVec,
    const LocalToGlobalMap& L2G, const GlobalElementIndex& elem_index) {

  size_t localsize = 0;

  // double *MMVec;
// 
  // VecGetArray (MassVec,& MMVec);
  const VecDouble& MMVec = MassVec;

  MMdiag.resize (getFields ().size ());

  for (size_t f = 0; f < getFields ().size (); f++) {
    localsize += getFieldDof (f);
    MMdiag[f].resize (getFieldDof (f), 0.);
    for (size_t a = 0; a < getFieldDof (f); a++) {
      MMdiag[f][a] = MMVec[L2G.map (f, a, elem_index)];
    }
  }

#ifdef DEBUG
  std::cerr << "MMdiag = " << std::endl;
  for (size_t f = 0; f < getFields().size(); ++f) {
    printIter (std::cerr, MMdiag[f].begin(), MMdiag[f].end());
  }
#endif

  // VecRestoreArray (MassVec,& MMVec);

}
;

/*
 void StandardAVI::print(const MatDouble& q) {
 for(size_t f=0;f < nfields; f++) {
 for(size_t a=0;a < getFieldDof(f); a++) {
 printf("elem %d,  q[%d][%d] = %f \n",elem_index,f,a,q[f][a]);
 }
 }
 };

 void StandardAVI::print(void) const {
 printf("elem %d, update time = %f \n",elem_index,getNextTimeStamp());
 };

 void StandardAVI::print_vals(void) const {
 size_t myid;
 MPI_Comm_rank(MPI_COMM_WORLD,& myid);
 printf("myid = %d, elem = %d, timeStep = %e, update_time = %e\n",
 myid,elem_index,timeStep,timeStamp);
 printf("connectivity = ");
 for(size_t i = 0; i < operation->getElement().
 getGeometry().getConnectivity().size(); i++) {
 printf("%d ",operation->getElement().getGeometry().getConnectivity()[i]);
 }
 printf("\n");
 for(size_t i = 0; i < MMdiag.size(); i++) {
 for(size_t j = 0; j < MMdiag[i].size(); j++)
 printf("%e ",MMdiag[i][j]);
 printf("\n");
 }
 };

 */



