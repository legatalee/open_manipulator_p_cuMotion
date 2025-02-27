﻿/*******************************************************************************
* Copyright 2019 ROBOTIS CO., LTD.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

/* Authors: Ryan Shim */

#include "open_manipulator_p_controller/open_manipulator_p_controller.hpp"

using namespace std::placeholders;
using namespace std::chrono_literals;

namespace open_manipulator_p_controller
{
OpenManipulatorPController::OpenManipulatorPController(std::string usb_port, std::string baud_rate)
: Node("open_manipulator_p_controller")
{
  /************************************************************
  ** Initialise ROS Parameters
  ************************************************************/
  init_parameters();
  
  /************************************************************
  ** Initialise variables
  ************************************************************/
  open_manipulator_p_.init_open_manipulator_p(sim_, usb_port, baud_rate, control_period_, with_gripper_);

  if (sim_ == false) RCLCPP_INFO(this->get_logger(), "Succeeded to Initialise OpenMANIPULATOR-P Controller");
  else RCLCPP_INFO(this->get_logger(), "Ready to Simulate OpenMANIPULATOR-P on Gazebo");

  /************************************************************
  ** Initialise ROS Publishers, Subscribers and Servers
  ************************************************************/
  init_publisher();
  init_subscriber();
  init_server();

  /************************************************************
  ** Start Process and Publish Threads
  ************************************************************/
  process_timer_ = this->create_wall_timer(10ms, std::bind(&OpenManipulatorPController::process_callback, this));
  publish_timer_ = this->create_wall_timer(10ms, std::bind(&OpenManipulatorPController::publish_callback, this));
}

OpenManipulatorPController::~OpenManipulatorPController()
{
  RCLCPP_INFO(this->get_logger(), "OpenMANIPULATOR-P Controller Terminated");
  // open_manipulator_p_.disableAllActuator(); // Commented out for safety reason
}

/********************************************************************************
** Init Functions
********************************************************************************/
void OpenManipulatorPController::init_parameters()
{
  // Declare parameters that may be set on this node
  this->declare_parameter("sim", false);
  this->declare_parameter("control_period", 0.010);
  this->declare_parameter("with_gripper", false);

  // Get parameter from yaml
  this->get_parameter_or<bool>("sim", sim_, false);
  this->get_parameter_or<double>("control_period", control_period_, 0.010);
  this->get_parameter_or<bool>("with_gripper", with_gripper_, false);
}

void OpenManipulatorPController::init_publisher()
{
  auto qos = rclcpp::QoS(rclcpp::KeepLast(10));

  // Publish States
  open_manipulator_p_states_pub_ = this->create_publisher<open_manipulator_msgs::msg::OpenManipulatorState>("states", qos);

  // Publish Joint States
  auto tools_name = open_manipulator_p_.getManipulator()->getAllToolComponentName();

  if (sim_ == false) // for actual robot
  {
    open_manipulator_p_joint_states_pub_ = this->create_publisher<sensor_msgs::msg::JointState>("joint_states", qos);
  }
  else // for virtual robot on Gazebo
  {
    auto joints_name = open_manipulator_p_.getManipulator()->getAllActiveJointComponentName();
    joints_name.resize(joints_name.size() + tools_name.size());
    joints_name.insert(joints_name.end(), tools_name.begin(), tools_name.end());

    for (auto const & name:joints_name)
    {
      auto pb = this->create_publisher<std_msgs::msg::Float64>(name + "_position/command", qos);
      gazebo_goal_joint_position_pub_.push_back(pb);
    }
  }

  // Publish Kinematics Pose
  for (auto const & name:tools_name)
  {
    auto pb = this->create_publisher<open_manipulator_msgs::msg::KinematicsPose>("kinematics_pose", qos);
    open_manipulator_p_kinematics_pose_pub_.push_back(pb);
  }
}

void OpenManipulatorPController::init_subscriber()
{
  auto qos = rclcpp::QoS(rclcpp::KeepLast(10));

  open_manipulator_p_option_sub_ = this->create_subscription<std_msgs::msg::String>(
    "option", qos, std::bind(&OpenManipulatorPController::open_manipulator_p_option_callback, this, _1));
}

void OpenManipulatorPController::init_server()
{
  goal_joint_space_path_server_ = this->create_service<open_manipulator_msgs::srv::SetJointPosition>(
    "goal_joint_space_path", std::bind(&OpenManipulatorPController::goal_joint_space_path_callback, this, _1, _2));
  goal_joint_space_path_to_kinematics_pose_server_ = this->create_service<open_manipulator_msgs::srv::SetKinematicsPose>(
    "goal_joint_space_path_to_kinematics_pose", std::bind(&OpenManipulatorPController::goal_joint_space_path_to_kinematics_pose_callback, this, _1, _2));
  goal_joint_space_path_to_kinematics_position_server_ = this->create_service<open_manipulator_msgs::srv::SetKinematicsPose>(
    "goal_joint_space_path_to_kinematics_position", std::bind(&OpenManipulatorPController::goal_joint_space_path_to_kinematics_position_callback, this, _1, _2));
  goal_joint_space_path_to_kinematics_orientation_server_ = this->create_service<open_manipulator_msgs::srv::SetKinematicsPose>(
    "goal_joint_space_path_to_kinematics_orientation", std::bind(&OpenManipulatorPController::goal_joint_space_path_to_kinematics_orientation_callback, this, _1, _2));
  goal_task_space_path_server_ = this->create_service<open_manipulator_msgs::srv::SetKinematicsPose>(
    "goal_task_space_path", std::bind(&OpenManipulatorPController::goal_task_space_path_callback, this, _1, _2));
  goal_task_space_path_position_only_server_ = this->create_service<open_manipulator_msgs::srv::SetKinematicsPose>(
    "goal_task_space_path_position_only", std::bind(&OpenManipulatorPController::goal_task_space_path_position_only_callback, this, _1, _2));
  goal_task_space_path_orientation_only_server_ = this->create_service<open_manipulator_msgs::srv::SetKinematicsPose>(
    "goal_task_space_path_orientation_only", std::bind(&OpenManipulatorPController::goal_task_space_path_orientation_only_callback, this, _1, _2));
  goal_joint_space_path_from_present_server_ = this->create_service<open_manipulator_msgs::srv::SetJointPosition>(
    "goal_joint_space_path_from_present", std::bind(&OpenManipulatorPController::goal_joint_space_path_from_present_callback, this, _1, _2));
  goal_task_space_path_from_present_server_ = this->create_service<open_manipulator_msgs::srv::SetKinematicsPose>(
    "goal_task_space_path_from_present", std::bind(&OpenManipulatorPController::goal_task_space_path_from_present_callback, this, _1, _2));
  goal_task_space_path_from_present_position_only_server_ = this->create_service<open_manipulator_msgs::srv::SetKinematicsPose>(
    "goal_task_space_path_from_present_position_only", std::bind(&OpenManipulatorPController::goal_task_space_path_from_present_position_only_callback, this, _1, _2));
  goal_task_space_path_from_present_orientation_only_server_ = this->create_service<open_manipulator_msgs::srv::SetKinematicsPose>(
    "goal_task_space_path_from_present_orientation_only", std::bind(&OpenManipulatorPController::goal_task_space_path_from_present_orientation_only_callback, this, _1, _2));
  goal_tool_control_server_ = this->create_service<open_manipulator_msgs::srv::SetJointPosition>(
    "goal_tool_control", std::bind(&OpenManipulatorPController::goal_tool_control_callback, this, _1, _2));
  set_actuator_state_server_ = this->create_service<open_manipulator_msgs::srv::SetActuatorState>(
    "set_actuator_state", std::bind(&OpenManipulatorPController::set_actuator_state_callback, this, _1, _2));
  goal_drawing_trajectory_server_ = this->create_service<open_manipulator_msgs::srv::SetDrawingTrajectory>(
    "goal_drawing_trajectory", std::bind(&OpenManipulatorPController::goal_drawing_trajectory_callback, this, _1, _2));
}

/*****************************************************************************
** Callback Functions for ROS Subscribers
*****************************************************************************/
void OpenManipulatorPController::open_manipulator_p_option_callback(const std_msgs::msg::String::SharedPtr msg)
{
  if (msg->data == "print_open_manipulator_p_setting")
    open_manipulator_p_.printManipulatorSetting();
}

/*****************************************************************************
** Callback Functions for ROS Servers
*****************************************************************************/
void OpenManipulatorPController::goal_joint_space_path_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetJointPosition::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetJointPosition::Response> res)
{
  std::vector <double> target_angle;

  for(uint8_t i = 0; i < req->joint_position.joint_name.size(); i ++)
    target_angle.push_back(req->joint_position.position.at(i));

  open_manipulator_p_.makeJointTrajectory(target_angle, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_joint_space_path_to_kinematics_pose_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Response> res)
{
  KinematicPose target_pose;
  target_pose.position[0] = req->kinematics_pose.pose.position.x;
  target_pose.position[1] = req->kinematics_pose.pose.position.y;
  target_pose.position[2] = req->kinematics_pose.pose.position.z;

  Eigen::Quaterniond q(req->kinematics_pose.pose.orientation.w,
                       req->kinematics_pose.pose.orientation.x,
                       req->kinematics_pose.pose.orientation.y,
                       req->kinematics_pose.pose.orientation.z);

  target_pose.orientation = math::convertQuaternionToRotationMatrix(q);

  open_manipulator_p_.makeJointTrajectory(req->end_effector_name, target_pose, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_joint_space_path_to_kinematics_position_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Response> res)
{
  KinematicPose target_pose;
  target_pose.position[0] = req->kinematics_pose.pose.position.x;
  target_pose.position[1] = req->kinematics_pose.pose.position.y;
  target_pose.position[2] = req->kinematics_pose.pose.position.z;

  open_manipulator_p_.makeJointTrajectory(req->end_effector_name, target_pose.position, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_joint_space_path_to_kinematics_orientation_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Response> res)
{
  KinematicPose target_pose;

  Eigen::Quaterniond q(req->kinematics_pose.pose.orientation.w,
                       req->kinematics_pose.pose.orientation.x,
                       req->kinematics_pose.pose.orientation.y,
                       req->kinematics_pose.pose.orientation.z);

  target_pose.orientation = math::convertQuaternionToRotationMatrix(q);

  open_manipulator_p_.makeJointTrajectory(req->end_effector_name, target_pose.orientation, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_task_space_path_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Response> res)
{
  KinematicPose target_pose;
  target_pose.position[0] = req->kinematics_pose.pose.position.x;
  target_pose.position[1] = req->kinematics_pose.pose.position.y;
  target_pose.position[2] = req->kinematics_pose.pose.position.z;

  Eigen::Quaterniond q(req->kinematics_pose.pose.orientation.w,
                       req->kinematics_pose.pose.orientation.x,
                       req->kinematics_pose.pose.orientation.y,
                       req->kinematics_pose.pose.orientation.z);

  target_pose.orientation = math::convertQuaternionToRotationMatrix(q);
  open_manipulator_p_.makeTaskTrajectory(req->end_effector_name, target_pose, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_task_space_path_position_only_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Response> res)
{
  Eigen::Vector3d position;
  position[0] = req->kinematics_pose.pose.position.x;
  position[1] = req->kinematics_pose.pose.position.y;
  position[2] = req->kinematics_pose.pose.position.z;

  open_manipulator_p_.makeTaskTrajectory(req->end_effector_name, position, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_task_space_path_orientation_only_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Response> res)
{
  Eigen::Quaterniond q(req->kinematics_pose.pose.orientation.w,
                       req->kinematics_pose.pose.orientation.x,
                       req->kinematics_pose.pose.orientation.y,
                       req->kinematics_pose.pose.orientation.z);

  Eigen::Matrix3d orientation = math::convertQuaternionToRotationMatrix(q);

  open_manipulator_p_.makeTaskTrajectory(req->end_effector_name, orientation, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_joint_space_path_from_present_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetJointPosition::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetJointPosition::Response> res)
{
  std::vector <double> target_angle;

  for(uint8_t i = 0; i < req->joint_position.joint_name.size(); i ++)
    target_angle.push_back(req->joint_position.position.at(i));

  open_manipulator_p_.makeJointTrajectoryFromPresentPosition(target_angle, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_task_space_path_from_present_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Response> res)
{
  KinematicPose target_pose;
  target_pose.position[0] = req->kinematics_pose.pose.position.x;
  target_pose.position[1] = req->kinematics_pose.pose.position.y;
  target_pose.position[2] = req->kinematics_pose.pose.position.z;

  Eigen::Quaterniond q(req->kinematics_pose.pose.orientation.w,
                       req->kinematics_pose.pose.orientation.x,
                       req->kinematics_pose.pose.orientation.y,
                       req->kinematics_pose.pose.orientation.z);

  target_pose.orientation = math::convertQuaternionToRotationMatrix(q);

  open_manipulator_p_.makeTaskTrajectoryFromPresentPose(req->planning_group, target_pose, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_task_space_path_from_present_position_only_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Response> res)
{
  Eigen::Vector3d position;
  position[0] = req->kinematics_pose.pose.position.x;
  position[1] = req->kinematics_pose.pose.position.y;
  position[2] = req->kinematics_pose.pose.position.z;

  open_manipulator_p_.makeTaskTrajectoryFromPresentPose(req->planning_group, position, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_task_space_path_from_present_orientation_only_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetKinematicsPose::Response> res)
{
  Eigen::Quaterniond q(req->kinematics_pose.pose.orientation.w,
                       req->kinematics_pose.pose.orientation.x,
                       req->kinematics_pose.pose.orientation.y,
                       req->kinematics_pose.pose.orientation.z);

  Eigen::Matrix3d orientation = math::convertQuaternionToRotationMatrix(q);

  open_manipulator_p_.makeTaskTrajectoryFromPresentPose(req->planning_group, orientation, req->path_time);

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_tool_control_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetJointPosition::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetJointPosition::Response> res)
{
  for(uint8_t i = 0; i < req->joint_position.joint_name.size(); i ++)
    open_manipulator_p_.makeToolTrajectory(req->joint_position.joint_name.at(i), req->joint_position.position.at(i));

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::set_actuator_state_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetActuatorState::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetActuatorState::Response> res)
{
  if (req->set_actuator_state == true) // enable actuators
  {
    log::println("Wait a second for actuator enable", "GREEN");
    open_manipulator_p_.enableAllActuator();
  }
  else // disable actuators
  {
    log::println("Wait a second for actuator disable", "GREEN");
    open_manipulator_p_.disableAllActuator();
  }

  res->is_planned = true;
  return;
}

void OpenManipulatorPController::goal_drawing_trajectory_callback(
  const std::shared_ptr<open_manipulator_msgs::srv::SetDrawingTrajectory::Request> req,
  const std::shared_ptr<open_manipulator_msgs::srv::SetDrawingTrajectory::Response> res)
{
  try
  {
    if (!req->drawing_trajectory_name.compare("circle"))
    {
      double draw_circle_arg[3];
      draw_circle_arg[0] = req->param[0];  // radius (m)
      draw_circle_arg[1] = req->param[1];  // revolution (rev)
      draw_circle_arg[2] = req->param[2];  // start angle position (rad)
      void* p_draw_circle_arg = &draw_circle_arg;

      open_manipulator_p_.makeCustomTrajectory(CUSTOM_TRAJECTORY_CIRCLE, req->end_effector_name, p_draw_circle_arg, req->path_time);
    }
    else if (!req->drawing_trajectory_name.compare("line"))
    {
      TaskWaypoint draw_line_arg;
      draw_line_arg.kinematic.position(0) = req->param[0]; // x axis (m)
      draw_line_arg.kinematic.position(1) = req->param[1]; // y axis (m)
      draw_line_arg.kinematic.position(2) = req->param[2]; // z axis (m)
      void *p_draw_line_arg = &draw_line_arg;

      open_manipulator_p_.makeCustomTrajectory(CUSTOM_TRAJECTORY_LINE, req->end_effector_name, p_draw_line_arg, req->path_time);
    }
    else if (!req->drawing_trajectory_name.compare("rhombus"))
    {
      double draw_rhombus_arg[3];
      draw_rhombus_arg[0] = req->param[0];  // radius (m)
      draw_rhombus_arg[1] = req->param[1];  // revolution (rev)
      draw_rhombus_arg[2] = req->param[2];  // start angle position (rad)
      void* p_draw_rhombus_arg = &draw_rhombus_arg;

      open_manipulator_p_.makeCustomTrajectory(CUSTOM_TRAJECTORY_RHOMBUS, req->end_effector_name, p_draw_rhombus_arg, req->path_time);
    }
    else if (!req->drawing_trajectory_name.compare("heart"))
    {
      double draw_heart_arg[3];
      draw_heart_arg[0] = req->param[0];  // radius (m)
      draw_heart_arg[1] = req->param[1];  // revolution (rev)
      draw_heart_arg[2] = req->param[2];  // start angle position (rad)
      void* p_draw_heart_arg = &draw_heart_arg;

      open_manipulator_p_.makeCustomTrajectory(CUSTOM_TRAJECTORY_HEART, req->end_effector_name, p_draw_heart_arg, req->path_time);
    }
    res->is_planned = true;
    return;
  }
  catch (rclcpp::exceptions::RCLError &e)
  {
    log::error("Failed to Create a Custom Trajectory");
  }
  return;
}

/********************************************************************************
** Functions related to processCallback 
********************************************************************************/
void OpenManipulatorPController::process_callback()   
{
  rclcpp::Clock clock(RCL_SYSTEM_TIME);
  rclcpp::Time present_time = clock.now();
  this->process(present_time.seconds());
}

void OpenManipulatorPController::process(double time)
{
  open_manipulator_p_.process_open_manipulator_p(time, sim_, with_gripper_);
}

/********************************************************************************
** Functions related to publishCallback 
********************************************************************************/
void OpenManipulatorPController::publish_callback()   
{
  if (sim_ == false) publish_joint_states();
  else publish_gazebo_command();

  publish_open_manipulator_p_states();
  publish_kinematics_pose();
}

void OpenManipulatorPController::publish_open_manipulator_p_states()
{
  open_manipulator_msgs::msg::OpenManipulatorState msg;
  if (open_manipulator_p_.getMovingState())
    msg.open_manipulator_moving_state = msg.IS_MOVING;
  else
    msg.open_manipulator_moving_state = msg.STOPPED;

  if (open_manipulator_p_.getActuatorEnabledState(JOINT_DYNAMIXEL))
    msg.open_manipulator_actuator_state = msg.ACTUATOR_ENABLED;
  else
    msg.open_manipulator_actuator_state = msg.ACTUATOR_DISABLED;

  open_manipulator_p_states_pub_->publish(msg);
}

void OpenManipulatorPController::publish_kinematics_pose()
{
  open_manipulator_msgs::msg::KinematicsPose msg;
  auto tools_name = open_manipulator_p_.getManipulator()->getAllToolComponentName();

  uint8_t index = 0;
  for (auto const & tools:tools_name)
  {
    KinematicPose pose = open_manipulator_p_.getKinematicPose(tools);
    msg.pose.position.x = pose.position[0];
    msg.pose.position.y = pose.position[1];
    msg.pose.position.z = pose.position[2];
    Eigen::Quaterniond orientation = math::convertRotationMatrixToQuaternion(pose.orientation);
    msg.pose.orientation.w = orientation.w();
    msg.pose.orientation.x = orientation.x();
    msg.pose.orientation.y = orientation.y();
    msg.pose.orientation.z = orientation.z();

    open_manipulator_p_kinematics_pose_pub_.at(index)->publish(msg);
    index++;
  }
}

void OpenManipulatorPController::publish_joint_states()
{
  sensor_msgs::msg::JointState msg;
  msg.header.stamp = rclcpp::Clock().now();

  auto joints_name = open_manipulator_p_.getManipulator()->getAllActiveJointComponentName();
  auto tools_name = open_manipulator_p_.getManipulator()->getAllToolComponentName();

  auto joint_value = open_manipulator_p_.getAllActiveJointValue();
  auto tool_value = open_manipulator_p_.getAllToolValue();

  for(uint8_t i = 0; i < joints_name.size(); i ++)
  {
    msg.name.push_back(joints_name.at(i));
    msg.position.push_back(joint_value.at(i).position);
    msg.velocity.push_back(joint_value.at(i).velocity);
    msg.effort.push_back(joint_value.at(i).effort);
  }

  for(uint8_t i = 0; i < tools_name.size(); i ++)
  {
    msg.name.push_back(tools_name.at(i));
    msg.position.push_back(tool_value.at(i).position);
    msg.velocity.push_back(0.0);
    msg.effort.push_back(0.0);
  }
  open_manipulator_p_joint_states_pub_->publish(msg);
}

void OpenManipulatorPController::publish_gazebo_command()
{
  JointWaypoint joint_value = open_manipulator_p_.getAllActiveJointValue();
  JointWaypoint tool_value = open_manipulator_p_.getAllToolValue();

  for(uint8_t i = 0; i < joint_value.size(); i ++)
  {
    std_msgs::msg::Float64 msg;
    msg.data = joint_value.at(i).position;
    gazebo_goal_joint_position_pub_.at(i)->publish(msg);
  }

  for(uint8_t i = 0; i < tool_value.size(); i ++)
  {
    std_msgs::msg::Float64 msg;
    msg.data = tool_value.at(i).position;
    gazebo_goal_joint_position_pub_.at(joint_value.size() + i)->publish(msg);
  }
}
}  // namespace open_manipulator_p_controller

/*****************************************************************************
** Main
*****************************************************************************/
int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);

  std::string usb_port = "/dev/ttyUSB0";
  std::string baud_rate = "1000000";
  if (argc >= 3)
  {
    usb_port = argv[1];
    baud_rate = argv[2];
    printf("port_name and baud_rate are set to %s, %s \n", usb_port.c_str(), baud_rate.c_str());
  }
  else
    printf("port_name and baud_rate are set to %s, %s \n", usb_port.c_str(), baud_rate.c_str());

  rclcpp::spin(std::make_shared<open_manipulator_p_controller::OpenManipulatorPController>(usb_port, baud_rate));
  rclcpp::shutdown();

  return 0;
}
