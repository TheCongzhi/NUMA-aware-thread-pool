# README

- [English](README.md)
- [简体中文](README.zh_CN.md)

This project implements a **thread pool** that runs on **MacOS** and **Linux**. The behavior of the thread pool adapts depending on the underlying CPU hardware.

- **On MacOS**: Since Apple Silicon uses **Unified Memory Architecture (UMA)**, the thread pool defaults to a UMA-based design.  
  (Note: **MacOS does not support NUMA architectures by default**.)

- **On Linux**: The thread pool adjusts based on the system’s CPU architecture. If the system supports **NUMA**, task scheduling and memory access strategies are optimized accordingly.

What is **NUMA**? See here: []()

### The POSIX Wrapper


