#ifndef _FEMAP_H
#define _FEMAP_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>

#include "FemapData.h"  

class Femap {
 public:
  size_t getNumMaterials()      const { return _materials.size(); };
  size_t getNumProperties()     const { return _properties.size(); };
  size_t getNumNodes()          const { return _nodes.size(); };
  size_t getNumElements()       const { return _elements.size(); };
  size_t getNumElements(size_t t)  const ; // no. of elements with topology indicator t
  size_t getNumConstraintSets() const { return _constraintSets.size(); };
  size_t getNumLoadSets()       const { return _loadSets.size(); };
  size_t getNumGroups()         const { return _groups.size(); };

  const std::vector<femapMaterial>&      getMaterials()      const { return _materials; };
  const std::vector<femapProperty>&      getProperties()     const { return _properties; };
  const std::vector<femapNode>&          getNodes()          const { return _nodes; };
  const std::vector<femapElement>&       getElements()       const { return _elements; };
  const std::vector<femapConstraintSet>& getConstraintSets() const { return _constraintSets; };
  const std::vector<femapLoadSet>&       getLoadSets()       const { return _loadSets; };
  const std::vector<femapGroup>&         getGroups()         const { return _groups; };

  void getElements(size_t t, std::vector<femapElement>& vout) const ; // gets elements with topology t

  const femapMaterial&      getMaterial(size_t i)      const { return _materials[i]; };
  const femapProperty&      getProperty(size_t i)      const { return _properties[i]; };
  const femapNode&          getNode(size_t i)          const { return _nodes[i]; };
  const femapElement&       getElement(size_t i)       const { return _elements[i]; };
  const femapConstraintSet& getConstraintSet(size_t i) const { return _constraintSets[i]; };
  const femapLoadSet&       getLoadSet(size_t i)       const { return _loadSets[i]; };
  const femapGroup&         getGroup(size_t i)         const { return _groups[i]; };

  int getMaterialId(size_t i)      const { return _materialIdMap.at (i); };
  int getPropertyId(size_t i)      const { return _propertyIdMap.at (i); };
  int getNodeId(size_t i)          const { return _nodeIdMap.at (i); };
  int getElementId(size_t i)       const { return _elementIdMap.at (i); };
  int getConstraintSetId(size_t i) const { return _constraintSetIdMap.at (i); };
  int getLoadSetId(size_t i)       const { return _loadSetIdMap.at (i); };
  int getGroupId(size_t i)         const { return _groupIdMap.at (i); };


 protected:
  std::vector<femapMaterial>      _materials;
  std::vector<femapProperty>      _properties;
  std::vector<femapNode>          _nodes;
  std::vector<femapElement>       _elements;
  std::vector<femapGroup>         _groups;
  std::vector<femapConstraintSet> _constraintSets;
  std::vector<femapLoadSet>       _loadSets;

  std::map<size_t,size_t>               _materialIdMap;
  std::map<size_t,size_t>               _propertyIdMap;
  std::map<size_t,size_t>               _nodeIdMap;
  std::map<size_t,size_t>               _elementIdMap;
  std::map<size_t,size_t>               _groupIdMap;
  std::map<size_t,size_t>               _constraintSetIdMap;
  std::map<size_t,size_t>               _loadSetIdMap;

 private:
  
};

class FemapInput : public Femap {
 public:
  FemapInput(const char* fileName);



 private:
  std::ifstream _ifs;

  void _readHeader();
  void _readProperty();
  void _readNodes();
  void _readElements();
  void _readGroups();
  void _readConstraints();
  void _readLoads();
  void _readMaterial();
// MO 1/9/01 begin
//void nextLine(int=1);
  void nextLine(int);
  void nextLine();
// MO 1/9/01 end
};

class FemapOutput : public Femap {
 public:
  FemapOutput(const char* fileName) : _ofs(fileName) {}

 private:
  std::ofstream _ofs;
};
#endif //_FEMAP_H
