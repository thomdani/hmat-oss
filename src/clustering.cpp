/*
  HMat-OSS (HMatrix library, open source software)

  Copyright (C) 2014-2015 Airbus Group SAS

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

  http://github.com/jeromerobert/hmat-oss
*/

#include "clustering.hpp"
#include "cluster_tree.hpp"
#include "common/my_assert.h"
#include "hmat_cpp_interface.hpp"

#include <algorithm>
#include <cstring>

namespace {

/*! \brief Compare two DOF indices based on their coordinates
 */
class IndicesComparator
{
private:
  const hmat::DofCoordinates * coordinates_;
  const int* group_index_;
  const int dimension_;
  const int axis_;

public:
  IndicesComparator(int axis, const hmat::ClusterData& data)
    : coordinates_(data.coordinates())
    , group_index_(data.group_index())
    , dimension_(data.coordinates()->dimension())
    , axis_(axis)
  {}
  bool operator() (int i, int j) {
    if (group_index_ == NULL || group_index_[i] == group_index_[j])
      return coordinates_->spanCenter(i, axis_) < coordinates_->spanCenter(j, axis_);
    return group_index_[i] < group_index_[j];
  }
};

/** @brief Compare two DOF based on their "large span" status */
class LargeSpanComparator {
    const hmat::DofCoordinates& coordinates_;
    double threshold_;
    int dimension_;
public:
	LargeSpanComparator(const hmat::DofCoordinates& coordinates,
        double threshold, int dimension)
        : coordinates_(coordinates), threshold_(threshold), dimension_(dimension){}
    bool operator()(int i, int j) {
        bool vi = coordinates_.spanDiameter(i, dimension_) > threshold_;
        bool vj = coordinates_.spanDiameter(j, dimension_) > threshold_;
        return vi < vj;
    }
};

hmat::AxisAlignedBoundingBox* getAxisAlignedBoundingBox(const hmat::ClusterTree& node) {
    hmat::AxisAlignedBoundingBox* bbox =
        static_cast<hmat::AxisAlignedBoundingBox*>(node.clusteringAlgoData_);
    if (bbox == NULL) {
        bbox = new hmat::AxisAlignedBoundingBox(node.data);
        node.clusteringAlgoData_ = bbox;
    }
    return bbox;
}

}

namespace hmat {

void
AxisAlignClusteringAlgorithm::sortByDimension(ClusterTree& node, int dim)
const
{
  int* myIndices = node.data.indices() + node.data.offset();
  std::stable_sort(myIndices, myIndices + node.data.size(), IndicesComparator(dim, node.data));
}

AxisAlignedBoundingBox*
AxisAlignClusteringAlgorithm::getAxisAlignedBoundingbox(const ClusterTree& node)
const
{
    return ::getAxisAlignedBoundingBox(node);
}

int
AxisAlignClusteringAlgorithm::largestDimension(const ClusterTree& node)
const
{
  int maxDim = -1;
  double maxSize = -1.0;
  AxisAlignedBoundingBox* bbox = getAxisAlignedBoundingbox(node);
  const int dimension = node.data.coordinates()->dimension();
  for (int i = 0; i < dimension; i++) {
    double size = (bbox->bbMax()[i] - bbox->bbMin()[i]);
    if (size > maxSize) {
      maxSize = size;
      maxDim = i;
    }
  }
  return maxDim;
}

double
AxisAlignClusteringAlgorithm::volume(const ClusterTree& node)
const
{
  AxisAlignedBoundingBox* bbox = getAxisAlignedBoundingbox(node);
  double result = 1.;
  const int dimension = node.data.coordinates()->dimension();
  for (int dim = 0; dim < dimension; dim++) {
    result *= (bbox->bbMax()[dim] - bbox->bbMin()[dim]);
  }
  return result;
}

void
AxisAlignClusteringAlgorithm::sort(ClusterTree& current, int axisIndex, int spatialDimension)
const
{
  int dim;
  if (axisIndex < 0) {
    dim = largestDimension(current);
  } else {
    if (spatialDimension < 0)
      spatialDimension = current.data.coordinates()->dimension();
    dim = ((axisIndex + current.depth) % spatialDimension);
  }
  sortByDimension(current, dim);
}

void
ClusteringAlgorithm::setMaxLeafSize(int maxLeafSize)
{
  maxLeafSize_ = maxLeafSize;
}

int
ClusteringAlgorithm::getMaxLeafSize() const
{
  if (maxLeafSize_ >= 0)
    return maxLeafSize_;
  const HMatSettings& settings = HMatSettings::getInstance();
  return settings.maxLeafSize;
}

int
ClusteringAlgorithm::getDivider() const
{
  return divider_;
}

void
ClusteringAlgorithm::setDivider(int divider) const
{
  divider_ = divider;
}

void
GeometricBisectionAlgorithm::partition(ClusterTree& current, std::vector<ClusterTree*>& children) const
{
  int dim;
  if (spatialDimension_ < 0) {
    spatialDimension_ = current.data.coordinates()->dimension();
  }
  if (axisIndex_ < 0) {
    dim = largestDimension(current);
  } else {
    dim = ((axisIndex_ + current.depth) % spatialDimension_);
  }
  sortByDimension(current, dim);
  AxisAlignedBoundingBox* bbox = getAxisAlignedBoundingbox(current);
  current.clusteringAlgoData_ = bbox;

  int previousIndex = 0;
  // Loop on 'divider_' = the number of children created
  for (int i=1 ; i<divider_ ; i++) {
    int middleIndex = previousIndex;
    double middlePosition = bbox->bbMin()[dim] + (i / (double)divider_) *
      (bbox->bbMax()[dim] - bbox->bbMin()[dim]);
    int* myIndices = current.data.indices() + current.data.offset();
    const double* coord = &current.data.coordinates()->get(0,0);
    while (middleIndex < current.data.size() && coord[myIndices[middleIndex]*spatialDimension_+dim] < middlePosition) {
      middleIndex++;
    }
    if (NULL != current.data.group_index())
    {
      // Ensure that we do not split inside a group
      const int* group_index = current.data.group_index() + current.data.offset();
      const int group(group_index[middleIndex]);
      if (group_index[middleIndex-1] == group)
      {
        int upper = middleIndex;
        int lower = middleIndex-1;
        while (upper < current.data.size() && group_index[upper] == group)
          ++upper;
        while (lower >= 0 && group_index[lower] == group)
          --lower;
        if (lower < 0 && upper == current.data.size())
        {
          // All degrees of freedom belong to the same group, this is fine
        }
        else if (lower < 0)
          middleIndex = upper;
        else if (upper == current.data.size())
          middleIndex = lower + 1;
        else if (coord[myIndices[upper]*spatialDimension_+dim] + coord[myIndices[lower]*spatialDimension_+dim] < 2.0 * middlePosition)
          middleIndex = upper;
        else
          middleIndex = lower + 1;
      }
    }
    if (middleIndex > previousIndex)
      children.push_back(current.slice(current.data.offset()+previousIndex, middleIndex-previousIndex));
    previousIndex = middleIndex;
  }
  // Add the last child
  children.push_back(current.slice(current.data.offset()+ previousIndex, current.data.size() - previousIndex));
}

void
GeometricBisectionAlgorithm::clean(ClusterTree& current) const
{
  delete static_cast<AxisAlignedBoundingBox*>(current.clusteringAlgoData_);
  current.clusteringAlgoData_ = NULL;
}

void
MedianBisectionAlgorithm::partition(ClusterTree& current, std::vector<ClusterTree*>& children) const
{
  sort(current, axisIndex_, spatialDimension_);
  int previousIndex = 0;
  // Loop on 'divider_' = the number of children created
  for (int i=1 ; i<divider_ ; i++) {
    int middleIndex = current.data.size() * i / divider_;
    if (NULL != current.data.group_index())
    {
      // Ensure that we do not split inside a group
      const int* group_index = current.data.group_index() + current.data.offset();
      const int group(group_index[middleIndex]);
      if (group_index[middleIndex-1] == group)
      {
        int upper = middleIndex;
        int lower = middleIndex-1;
        while (upper < current.data.size() && group_index[upper] == group)
          ++upper;
        while (lower >= 0 && group_index[lower] == group)
          --lower;
        if (lower < 0 && upper == current.data.size())
        {
          // All degrees of freedom belong to the same group, this is fine
        }
        else if (lower < 0)
          middleIndex = upper;
        else if (upper == current.data.size())
          middleIndex = lower + 1;
        else if (upper + lower < 2 * middleIndex)
          middleIndex = upper;
        else
          middleIndex = lower + 1;
      }
    }
    if (middleIndex > previousIndex)
      children.push_back(current.slice(current.data.offset()+previousIndex, middleIndex-previousIndex));
    previousIndex = middleIndex;
  }
  // Add the last child
  children.push_back(current.slice(current.data.offset()+ previousIndex, current.data.size() - previousIndex));
}

void
MedianBisectionAlgorithm::clean(ClusterTree& current) const
{
  delete static_cast<AxisAlignedBoundingBox*>(current.clusteringAlgoData_);
  current.clusteringAlgoData_ = NULL;
}

void
HybridBisectionAlgorithm::partition(ClusterTree& current, std::vector<ClusterTree*>& children) const
{
  // We first split tree node with an MedianBisectionAlgorithm instance, and compute
  // ratios of volume of children node divided by volume of current node.  If any ratio
  // is larger than a given threshold, this splitting is discarded and replaced by
  // a GeometricBisectionAlgorithm instead.
  medianAlgorithm_.partition(current, children);
  if (children.size() < 2)
    return;
  double currentVolume = volume(current);
  double maxVolume = 0.0;
  for (std::vector<ClusterTree*>::const_iterator cit = children.begin(); cit != children.end(); ++cit)
  {
    if (*cit != NULL)
      maxVolume = std::max(maxVolume, volume(**cit));
  }
  if (maxVolume > thresholdRatio_*currentVolume)
  {
    children.clear();
    geometricAlgorithm_.partition(current, children);
  }
}

void
HybridBisectionAlgorithm::clean(ClusterTree& current) const
{
  medianAlgorithm_.clean(current);
  geometricAlgorithm_.clean(current);
}

void
VoidClusteringAlgorithm::partition(ClusterTree& current, std::vector<ClusterTree*>& children) const
{
  if (current.depth % 2 == 0)
  {
    algo_->partition(current, children);
  } else {
    children.push_back(current.slice(current.data.offset(), current.data.size()));
    for (int i=1 ; i<divider_ ; i++)
      children.push_back(current.slice(current.data.offset() + current.data.size(), 0));
  }
}

void
VoidClusteringAlgorithm::clean(ClusterTree& current) const
{
  algo_->clean(current);
}

void
ShuffleClusteringAlgorithm::partition(ClusterTree& current, std::vector<ClusterTree*>& children) const
{
  algo_->partition(current, children);
  ++divider_;
  if (divider_ > toDivider_)
      divider_ = fromDivider_;
  setDivider(divider_);
}

void
ShuffleClusteringAlgorithm::clean(ClusterTree& current) const
{
  algo_->clean(current);
}

ClusterTreeBuilder::ClusterTreeBuilder(const ClusteringAlgorithm& algo)
{
  algo_.push_front(std::pair<int, ClusteringAlgorithm*>(0, algo.clone()));
}

ClusterTreeBuilder::~ClusterTreeBuilder()
{
  for (std::list<std::pair<int, ClusteringAlgorithm*> >::iterator it = algo_.begin(); it != algo_.end(); ++it)
  {
    delete it->second;
    it->second = NULL;
  }
}

ClusterTree*
ClusterTreeBuilder::build(const DofCoordinates& coordinates, int* group_index) const
{
  DofData* dofData = new DofData(coordinates, group_index);
  ClusterTree* rootNode = new ClusterTree(dofData);

  divide_recursive(*rootNode);
  clean_recursive(*rootNode);
  // Update reverse mapping
  int* indices_i2e = rootNode->data.indices();
  int* indices_e2i = rootNode->data.indices_rev();

  for (int i = 0; i < rootNode->data.size(); ++i) {
    indices_e2i[indices_i2e[i]] = i;
  }
  return rootNode;
}

void
ClusterTreeBuilder::clean_recursive(ClusterTree& current) const
{
  ClusteringAlgorithm* algo = getAlgorithm(current.depth);
  algo->clean(current);
  if (!current.isLeaf())
  {
    for (int i = 0; i < current.nrChild(); ++i)
    {
      if (current.getChild(i))
        clean_recursive(*((ClusterTree*)current.getChild(i)));
    }
  }
}

ClusteringAlgorithm*
ClusterTreeBuilder::getAlgorithm(int depth) const
{
  ClusteringAlgorithm* last = NULL;
  for (std::list<std::pair<int, ClusteringAlgorithm*> >::const_iterator it = algo_.begin(); it != algo_.end(); ++it)
  {
    if (it->first <= depth)
      last = it->second;
    else
      break;
  }
  return last;
}

ClusterTreeBuilder&
ClusterTreeBuilder::addAlgorithm(int depth, const ClusteringAlgorithm& algo)
{
  for (std::list<std::pair<int, ClusteringAlgorithm*> >::iterator it = algo_.begin(); it != algo_.end(); ++it)
  {
    if (it->first > depth)
    {
      algo_.insert(it, std::pair<int, ClusteringAlgorithm*>(depth, algo.clone()));
      return * this;
    }
  }
  algo_.insert(algo_.end(), std::pair<int, ClusteringAlgorithm*>(depth, algo.clone()));
  return *this;
}

void
ClusterTreeBuilder::divide_recursive(ClusterTree& current) const
{
  ClusteringAlgorithm* algo = getAlgorithm(current.depth);
  if (current.data.size() <= algo->getMaxLeafSize())
    return;

  // Sort degrees of freedom and partition current node
  std::vector<ClusterTree*> children;
  algo->partition(current, children);
  for (size_t i = 0; i < children.size(); ++i)
  {
    current.insertChild(i, children[i]);
    divide_recursive(*children[i]);
  }
}

SpanClusteringAlgorithm::SpanClusteringAlgorithm(
    const ClusteringAlgorithm &algo, double ratio):
    algo_(algo), ratio_(ratio){}

std::string SpanClusteringAlgorithm::str() const {
    return "SpanClusteringAlgorithm";
}
ClusteringAlgorithm* SpanClusteringAlgorithm::clone() const {
    return new SpanClusteringAlgorithm(algo_, ratio_);
}

void SpanClusteringAlgorithm::partition(
    ClusterTree& current, std::vector<ClusterTree*>& children) const {
    int offset = current.data.offset();
    int* indices = current.data.indices() + offset;
    const DofCoordinates & coords = *current.data.coordinates();
    int n = current.data.size();
    assert(n + offset <= current.data.coordinates()->numberOfDof());
    AxisAlignedBoundingBox * aabb = ::getAxisAlignedBoundingBox(current);
    int greatestDim = aabb->greatestDim();
    double threshold = aabb->extends(greatestDim) * ratio_;
    // move large span at the end of the indices array
    LargeSpanComparator comparator(coords, threshold, greatestDim);
    std::stable_sort(indices, indices + n, comparator);
    // create the large span cluster
    int i = n - 1;
    while(i >= 0 && coords.spanDiameter(indices[i], greatestDim) > threshold)
        i--;
    ClusterTree * largeSpanCluster = i < n - 1 ? current.slice(offset + i + 1, n - i - 1) : NULL;
    // Call the delegate algorithm with a temporary cluster
    // containing only small span DOFs.
    ClusterTree * smallSpanCluster = i >= 0 ? current.slice(offset, i + 1) : NULL;
    if(smallSpanCluster != NULL) {
        algo_.partition(*smallSpanCluster, children);
        // avoid dofData_ deletion
        smallSpanCluster->father = smallSpanCluster;
        delete smallSpanCluster;
    }
    if(largeSpanCluster != NULL && !children.empty())
        children.push_back(largeSpanCluster);
}

}  // end namespace hmat
