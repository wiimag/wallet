/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#pragma once

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;

void glfw_set_window_center(GLFWwindow* window);

GLFWwindow* glfw_create_window_geometry(const char* window_title);

void glfw_save_window_geometry(GLFWwindow* window);

GLFWmonitor* glfw_find_window_monitor(GLFWwindow* window);

GLFWmonitor* glfw_find_window_monitor(int window_x, int window_y);

bool glfw_is_window_focused(GLFWwindow* window);

bool glfw_is_any_mouse_button_down(GLFWwindow* window);
