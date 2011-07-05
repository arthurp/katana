/*
 * P1nDElement.h
 *
 *  Created on: Jun 13, 2011
 *      Author: amber
 */

#ifndef P1NDELEMENT_H_
#define P1NDELEMENT_H_

#include <vector>
#include <cassert>

#include "AuxDefs.h"
#include "Element.h"
#include "ElementBoundaryTrace.h"
#include "ElementGeometry.h"

template <size_t NF>
class P1nDElement: public Element_ {
private:
  const ElementGeometry& elemGeom;

public:
  P1nDElement (const ElementGeometry& _elemGeom) : Element_(), elemGeom(_elemGeom) {

  }

  P1nDElement (const P1nDElement& that) : Element_(that), elemGeom (that.elemGeom) {

  }

  virtual const size_t getNumFields () const {
    return NF;
  }

  virtual const ElementGeometry& getGeometry () const {
    return elemGeom;
  }

protected:
  size_t getFieldShapes (size_t field) const {
    return 0;
  }
};

template <size_t NF>
class P1nDTrace: public P1nDElement<NF> {
public:
  //! Range of FaceIndices available to enumerate faces\n
  //! When providing a FaceLabel as an argument there is automatic
  //! control of its range
  enum FaceLabel {
    FaceOne, FaceTwo, FaceThree, FaceFour
  };

  //! TwoDofs indicates Segment<2> boundary elements, with two dofs per field \n
  //! ThreeDofs indicates Triangle<2> boundary elements, with three dofs per field. The
  //! shape functions in P12DElement are just evaluated at quadrature points on each face\n
  enum ShapeType {
    TwoDofs, ThreeDofs, FourDofs
  };

  P1nDTrace (const P1nDElement<NF>& baseElem):
    P1nDElement<NF>(baseElem) {

  }

  P1nDTrace (const P1nDTrace<NF>& that):
    P1nDElement<NF> (that) {

  }


};


template <size_t NF>
class P1nDBoundaryTraces: public ElementBoundaryTraces_ {
private:
  MatDouble normals;

public:
  typedef typename P1nDTrace<NF>::FaceLabel FaceLabel;
  typedef typename P1nDTrace<NF>::ShapeType ShapeType;

  P1nDBoundaryTraces (const P1nDElement<NF>& baseElem,
      const std::vector<FaceLabel>& faceLabels, const ShapeType& shapeType) :
    ElementBoundaryTraces_ () {

    assert (faceLabels.size() == baseElem.getGeometry().getNumFaces ());

    for (size_t i = 0; i < faceLabels.size (); ++i) {
      const P1nDTrace<NF>* fTrace = makeTrace (baseElem, faceLabels[i], shapeType);
      addFace (fTrace, i);
    }

    normals.resize (getNumTraceFaces ());

    for (size_t i = 0; i < getNumTraceFaces (); ++i) {
      baseElem.getGeometry ().computeNormal (getTraceFaceIds ()[i], normals[i]);
    }

  }

  P1nDBoundaryTraces (const P1nDBoundaryTraces<NF>& that) :
    ElementBoundaryTraces_ (that), normals (that.normals) {

  }

  const std::vector<double> & getNormal (size_t FaceIndex) const {
    return normals[FaceIndex];
  }

protected:
  virtual const P1nDTrace<NF>* makeTrace (const P1nDElement<NF>& baseElem,
      const FaceLabel& flabel, const ShapeType& shType) const = 0;

};


/**
 \brief StandardP1nDMap class: standard local to global map when only P12D type of
 Elements are used.

 StandardP1nDMap assumes that\n
 1) The GlobalNodalIndex of a node is an size_t\n
 2) All degrees of freedom are associated with nodes, and their values for each
 node ordered consecutively according to the field number.

 Consequently, the GlobalDofIndex of the degrees of freedom of node N with NField
 fields are given by

 (N-1)*NF + field-1, where \f$ 1 \le \f$ fields \f$ \le \f$ NF
 */

class StandardP1nDMap: public LocalToGlobalMap {
private:
  const std::vector<Element*>& elementArray;

public:
  StandardP1nDMap (const std::vector<Element*>& _elementArray)
    : LocalToGlobalMap (), elementArray (_elementArray) {

  }

  StandardP1nDMap (const StandardP1nDMap& that) :
    LocalToGlobalMap (that), elementArray (that.elementArray) {

  }

  virtual StandardP1nDMap* clone () const {
    return new StandardP1nDMap (*this);
  }

  inline const GlobalDofIndex map (size_t field, size_t dof, const GlobalElementIndex& ElementMapped) const {
    const Element* elem = elementArray[ElementMapped];
    // we subtract 1 from node ids in 1-based node numbering
    // return elem->getNumFields () * (elem-> getGeometry ().getConnectivity ()[dof] - 1) + field;
    // no need to subtract 1 with 0-based node numbering
    return elem->getNumFields () * (elem-> getGeometry ().getConnectivity ()[dof]) + field;
  }


  inline const size_t getNumElements () const {
    return elementArray.size ();
  }

  inline const size_t getNumFields (const GlobalElementIndex & ElementMapped) const {
    return elementArray[ElementMapped]->getNumFields ();
  }
  inline const size_t getNumDof (const GlobalElementIndex & ElementMapped, size_t field) const {
    return elementArray[ElementMapped]->getDof (field);
  }

  const size_t getTotalNumDof () const {
    GlobalNodalIndex MaxNodeNumber = 0;

    for (size_t e = 0; e < elementArray.size (); e++) {
      const std::vector<GlobalNodalIndex>& conn = elementArray[e]->getGeometry ().getConnectivity ();

      for (size_t a = 0; a < conn.size (); ++a) {
        if (conn[a] > MaxNodeNumber) {
          MaxNodeNumber = conn[a];
        }
      }
    }

    // return maxNode * elementArray.get (0).numFields ();
    // add 1 here since nodes are number 0 .. numNodes-1 in 0-based node numbering
    return static_cast<size_t> (MaxNodeNumber + 1) * elementArray[0]->getNumFields ();
  }

protected:
  //! Access to ElementArray  for derived classes
  const std::vector<Element *> & getElementArray () const {
    return elementArray;
  }

};

#endif /* P1NDELEMENT_H_ */
