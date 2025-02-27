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

/* Authors: Darby Lim, Hye-Jong KIM, Ryan Shim, Yong-Ho Na */

#ifndef OPEN_MANIPULTOR_PRO_HPP
#define OPEN_MANIPULTOR_PRO_HPP

#include "custom_trajectory.hpp"
#include "dynamixel.hpp"
#include "kinematics.hpp"

#define CUSTOM_TRAJECTORY_SIZE 4
#define CUSTOM_TRAJECTORY_LINE    "custom_trajectory_line"
#define CUSTOM_TRAJECTORY_CIRCLE  "custom_trajectory_circle"
#define CUSTOM_TRAJECTORY_RHOMBUS "custom_trajectory_rhombus"
#define CUSTOM_TRAJECTORY_HEART   "custom_trajectory_heart"

#define JOINT_DYNAMIXEL "joint_dxl"
#define TOOL_DYNAMIXEL  "tool_dxl"

#define X_AXIS robotis_manipulator::math::vector3(1.0, 0.0, 0.0)
#define Y_AXIS robotis_manipulator::math::vector3(0.0, 1.0, 0.0)
#define Z_AXIS robotis_manipulator::math::vector3(0.0, 0.0, 1.0)

class OpenManipulatorP : public robotis_manipulator::RobotisManipulator
{
 public:
  OpenManipulatorP();
  virtual ~OpenManipulatorP();

  void init_open_manipulator_p(
    bool sim, 
    STRING usb_port = "/dev/ttyUSB0", 
    STRING baud_rate = "1000000", 
    float control_loop_time = 0.010, 
    bool with_gripper = false,
    std::vector<uint8_t> dxl_id = {1, 2, 3, 4, 5, 6, 7});
  void process_open_manipulator_p(double present_time, bool sim, bool with_gripper = false);
  JointWaypoint distance_to_angle(JointWaypoint distance);
  JointWaypoint angle_to_distance(JointWaypoint angle);

 private:
  robotis_manipulator::Kinematics *kinematics_;
  robotis_manipulator::JointActuator *actuator_;
  robotis_manipulator::ToolActuator *tool_;
  robotis_manipulator::CustomTaskTrajectory *custom_trajectory_[CUSTOM_TRAJECTORY_SIZE];
};
#endif // OPEN_MANIPULTOR_PRO_HPP
