#include <memory>
#include <autoware_msgs/Waypoint.h>

#include <geometry_msgs/TransformStamped.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>

#include <grid_map_ros/GridMapRosConverter.hpp>
#include <grid_map_ros/grid_map_ros.hpp>
#include <grid_map_msgs/GridMap.h>

#include <distance_transform/distance_transform.hpp>

#include "modified_reference_path_generator.h"

// struct Node
// { 
//   Eigen::Vector2d p;
//   double r;
//   double g;
//   double h;
//   double f;
// };

class Node
{
public:
  Node();
  ~Node();
  
  Eigen::Vector2d p;
  double r;
  double g;
  double h;
  double f;
  // Node* parent_node;
  std::shared_ptr<Node> parent_node;
};

Node::Node()
{
}

Node::~Node()
{
}

struct PathPoint
{
  Eigen::Vector2d position;
  double clearance;
  double curvature;
};



//TODO: make namespace/file for utility method
//TODO: better naming 
double calculate2DDistace(const Eigen::Vector2d& point1,
                          const Eigen::Vector2d& point2)
{
  double dx = point1(0) - point2(0);
  double dy = point1(1) - point2(1);
  double distance = std::sqrt(std::pow(dx, 2)+std::pow(dy,2));
  return distance;
}

bool compareF(Node lhs, Node rhs) { return lhs.f < rhs.f; }

std::vector<Node> expandNode(Node& parent_node, 
                             const grid_map::GridMap& clearence_map,
                             const Node& goal_node)
{
  const double min_r = 1.6;
  const double max_r = 10;
  
  //r min max
  double current_r;
  if(parent_node.r < min_r)
  {
    current_r = min_r;
  }
  else if(parent_node.r > max_r)
  {
    current_r = max_r;
  }
  else
  {
    current_r = parent_node.r;
  }
  
  
  std::vector<Node> child_nodes;
  Eigen::Vector2d delta_child_p;
  delta_child_p << current_r,
                   0;
  double delta_theta = 2*M_PI/36.0;
  for(double theta = 0; theta < 2*M_PI; theta += delta_theta)
  {
    // std::cerr << "theta " << theta << std::endl;
    Eigen::Matrix2d rotation;
    rotation << std::cos(theta), - std::sin(theta),
                std::sin(theta),   std::cos(theta);
    // std::cerr << "rotation " << rotation << std::endl;
    Eigen::Vector2d rotated_delta = rotation * delta_child_p;
    // std::cerr << "aaaaa " << rotated_delta << std::endl;
    Node child_node;
    child_node.p = rotated_delta + parent_node.p;
    try 
    {
      double tmp_r = clearence_map.atPosition(clearence_map.getLayers().back(),
                                             child_node.p)*0.1;
      double r = std::min(tmp_r, max_r);
      if(r < min_r)
      {
        continue;
      }
      child_node.r = r;
    }
    catch (const std::out_of_range& e) 
    {
      continue;
    }
    child_node.g = parent_node.g + current_r;
    child_node.h = calculate2DDistace(child_node.p, goal_node.p);
    child_node.f = child_node.g + child_node.h;
    child_node.parent_node = std::make_shared<Node>(parent_node);
    child_nodes.push_back(child_node);
  }
  return child_nodes;
}

bool isOverlap(Node node1, Node node2)
{
  double distance = calculate2DDistace(node1.p, node2.p);
  double max_r = std::max(node1.r, node2.r);
  double min_r = std::min(node1.r, node2.r);
  if((distance - max_r) < 0.5*min_r)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool nodeExistInClosedNodes(Node node, std::vector<Node> closed_nodes)
{
  for(const auto& closed_node: closed_nodes)
  {
    double distance = calculate2DDistace(node.p, closed_node.p);
    if(distance < closed_node.r)
    {
      return true;
    }
  }
  return false;
}

ModifiedReferencePathGenerator::ModifiedReferencePathGenerator(/* args */)
{
}

ModifiedReferencePathGenerator::~ModifiedReferencePathGenerator()
{
}

double ModifiedReferencePathGenerator::calculateCurvatureFromThreePoints(
          Eigen::Vector2d& path_point1,
          Eigen::Vector2d& path_point2,
          Eigen::Vector2d& path_point3)
{
  double x1 = path_point1(0);
  double y1 = path_point1(1);
  double x2 = path_point2(0);
  double y2 = path_point2(1);
  double x3 = path_point3(0);
  double y3 = path_point3(1);
  
  double a =  x1 * (y2 - y3) - y1 * (x2 - x3) + x2 * y3 - x3 * y2;

  double b = (x1 * x1 + y1 * y1) * (y3 - y2) 
          + (x2 * x2 + y2 * y2) * (y1 - y3)
          + (x3 * x3 + y3 * y3) * (y2 - y1);

  double c = (x1 * x1 + y1 * y1) * (x2 - x3) 
          + (x2 * x2 + y2 * y2) * (x3 - x1) 
          + (x3 * x3 + y3 * y3) * (x1 - x2);

  
  double x = -b / (2 * a);
  double y = -c / (2 * a);
  double r = std::sqrt(std::pow(x - x1, 2) + std::pow(y - y1, 2));
  
  // using cross product
  // https://stackoverflow.com/questions/243945/calculating-a-2d-vectors-cross-product
  double plus_or_minus_sign = (x1 - x2)*(y3 - y2) - (y1 - y2)*(x3 - x2);
  Eigen::Vector2d v1, v2;
  v1 << x1 - x2, y1 - y2;
  v2 << x3 - x2, y3 - y2;
  double yaw_by_cos = std::acos(v1.dot(v2)/(v1.norm()*v2.norm()));
  
  double curvature = 0;
  //straight check
  if(yaw_by_cos > (M_PI- 0.01))
  {
    curvature = 0;
  }
  else if(plus_or_minus_sign > 0)
  {
    curvature = -1/r;
  }
  else
  {
    curvature = 1/r;
  }
  return curvature;
}

bool ModifiedReferencePathGenerator::calculateCurvatureForPathPoints(
  std::vector<PathPoint>& path_points)
{
  //calculateCurvatureFromFourPoints
  for(size_t i = 1; i < (path_points.size() - 1); i++)
  {
    double curvature = calculateCurvatureFromThreePoints(
                     path_points[i-1].position,
                     path_points[i].position,
                     path_points[i+1].position);
    path_points[i].curvature = curvature;
  }
  if(path_points.size() > 1)
  {
    path_points.front().curvature = path_points[1].curvature;
    path_points.back().curvature = path_points[path_points.size()-1].curvature;
  }
  return true;
}

void ModifiedReferencePathGenerator::generateModifiedReferencePath(
    grid_map::GridMap& clearance_map, 
    const geometry_msgs::Point& start_point, 
    const geometry_msgs::Point& goal_point,
    const geometry_msgs::TransformStamped& lidar2map_tf, 
    const geometry_msgs::TransformStamped& map2lidar_tf,
    std::vector<autoware_msgs::Waypoint>& modified_reference_path,
    sensor_msgs::PointCloud2& debug_pointcloud_clearance_map)
{
  std::cerr << "aaaa "  << std::endl;
  std::string layer_name = clearance_map.getLayers().back();
  grid_map::Matrix data = clearance_map.get(layer_name);

  // grid_length y and grid_length_x respectively
  dope::Index2 size({ 100, 300 });
  dope::Grid<float, 2> f(size);
  dope::Grid<dope::SizeType, 2> indices(size);
  bool is_empty_cost = true;
  for (dope::SizeType i = 0; i < size[0]; ++i)
  {
    for (dope::SizeType j = 0; j < size[1]; ++j)
    {
      if (data(i * size[1] + j) > 0.01)
      {
        f[i][j] = 0.0f;
        is_empty_cost = false;
      }
      else
      {
        f[i][j] = std::numeric_limits<float>::max();
      }
    }
  }

  // Note: this is necessary at least at the first distance transform execution
  // and every time a reset is desired; it is not, instead, when updating
  dt::DistanceTransform::initializeIndices(indices);
  dt::DistanceTransform::distanceTransformL2(f, f, false, 1);

  for (dope::SizeType i = 0; i < size[0]; ++i)
  {
    for (dope::SizeType j = 0; j < size[1]; ++j)
    {
      if (is_empty_cost)
      {
        data(i * size[1] + j) = 1;
      }
      else
      {
        data(i * size[1] + j) = f[i][j];
      }
    }
  }

  clearance_map[layer_name] = data;
  grid_map::GridMapRosConverter::toPointCloud(clearance_map, layer_name, debug_pointcloud_clearance_map);

  geometry_msgs::Point start_point_in_lidar_tf, goal_point_in_lidar_tf;
  tf2::doTransform(start_point, start_point_in_lidar_tf, map2lidar_tf);
  tf2::doTransform(goal_point, goal_point_in_lidar_tf, map2lidar_tf);
  
  Eigen::Vector2d start_p, goal_p;
  start_p(0) = start_point_in_lidar_tf.x; 
  start_p(1) = start_point_in_lidar_tf.y; 
  goal_p(0) = goal_point_in_lidar_tf.x; 
  goal_p(1) = goal_point_in_lidar_tf.y; 
  
  std::vector<Node> s_open;
  Node* a = new Node();
  Node initial_node;
  double clearance_to_m = 0.1;
  const double min_r = 1.6;
  const double max_r = 10;
  initial_node.p = start_p;
  double initial_r = clearance_map.atPosition(layer_name, initial_node.p) * clearance_to_m ;
  if(initial_r < min_r)
  {
    initial_r = min_r;
  }
  else if(initial_r > max_r)
  {
    initial_r = max_r;
  }
  initial_node.r = initial_r;
  initial_node.g = 0;
  initial_node.h = calculate2DDistace(initial_node.p, goal_p);
  initial_node.f = initial_node.g + initial_node.h;
  initial_node.parent_node = nullptr;
  s_open.push_back(initial_node);
  
  Node goal_node;
  goal_node.p = goal_p;
  double goal_r = clearance_map.atPosition(layer_name, goal_node.p) * clearance_to_m ;
  if(goal_r < min_r)
  {
    goal_r = min_r;
  }
  else if(goal_r > max_r)
  {
    goal_r = max_r;
  }
  goal_node.r = goal_r;
  goal_node.g = 0;
  goal_node.h = 0;
  goal_node.f = 0;
  
  double f_goal = std::numeric_limits<double>::max();
  
  std::vector<Node> s_closed;
  while(!s_open.empty())
  {
    std::sort(s_open.begin(), s_open.end(), compareF);
    Node lowest_f_node = s_open.front();
    s_open.erase(s_open.begin());
    if(f_goal < lowest_f_node.f)
    {
      break;
    }
    else if(nodeExistInClosedNodes(lowest_f_node, s_closed))
    {
      continue;
    }
    else
    {
      std::vector<Node> child_nodes = 
          expandNode(lowest_f_node, clearance_map, goal_node);
      s_open.insert(s_open.end(),
                    child_nodes.begin(),
                    child_nodes.end());
      s_closed.push_back(lowest_f_node);
      if(isOverlap(lowest_f_node, goal_node))
      {
        f_goal = lowest_f_node.f;
      }
    }
  }
  
  std::vector<PathPoint> path_points;
  
  autoware_msgs::Waypoint start_waypoint;
  start_waypoint.pose.pose.position = start_point;
  start_waypoint.pose.pose.orientation.w = 1.0;
  modified_reference_path.push_back(start_waypoint);
  PathPoint start_path_point;
  start_path_point.position(0) = start_point.x;
  start_path_point.position(1) = start_point.y;
  try 
  {
    double tmp_r = clearance_map.atPosition(clearance_map.getLayers().back(),
                                            start_p)*0.1;
    double r = std::min(tmp_r, max_r);
    if(r < min_r)
    {
      r = min_r;
      std::cerr << "start point's clearance is wrong "  << std::endl;
    }
    start_path_point.clearance = r;
  }
  catch (const std::out_of_range& e) 
  {
    std::cerr << "WARNING: could not find clearance for start point " << std::endl;
  }
  start_path_point.curvature = 0;
  path_points.push_back(start_path_point);
  
  
  Node current_node = s_closed.back();
  while(current_node.parent_node != nullptr)
  {
    geometry_msgs::Pose pose_in_lidar_tf;
    pose_in_lidar_tf.position.x = current_node.p(0);
    pose_in_lidar_tf.position.y = current_node.p(1);
    pose_in_lidar_tf.position.z = start_point_in_lidar_tf.z;
    pose_in_lidar_tf.orientation.w = 1.0;
    geometry_msgs::Pose pose_in_map_tf;
    tf2::doTransform(pose_in_lidar_tf, pose_in_map_tf, lidar2map_tf);
    autoware_msgs::Waypoint waypoint;
    waypoint.pose.pose = pose_in_map_tf;
    modified_reference_path.push_back(waypoint);
    
    PathPoint path_point;
    path_point.position = current_node.p;
    path_point.clearance = current_node.r;
    path_point.curvature = 0;
    path_points.push_back(path_point);
    
    current_node = *current_node.parent_node;
    
  }
  
  autoware_msgs::Waypoint goal_waypoint;
  goal_waypoint.pose.pose.position = goal_point;
  goal_waypoint.pose.pose.orientation.w = 1.0;
  modified_reference_path.push_back(goal_waypoint);
  
  PathPoint goal_path_point;
  goal_path_point.position(0) = goal_point.x;
  goal_path_point.position(1) = goal_point.y;
  try 
  {
    double tmp_r = clearance_map.atPosition(clearance_map.getLayers().back(),
                                            goal_p)*0.1;
    double r = std::min(tmp_r, max_r);
    if(r < min_r)
    {
      r = min_r;
      std::cerr << "start point's clearance is wrong "  << std::endl;
    }
    goal_path_point.clearance = r;
  }
  catch (const std::out_of_range& e) 
  {
    std::cerr << "WARNING: could not find clearance for goal point " << std::endl;
  }
  goal_path_point.curvature = 0;
  path_points.push_back(goal_path_point);
  
  calculateCurvatureForPathPoints(path_points);
  
  
}
