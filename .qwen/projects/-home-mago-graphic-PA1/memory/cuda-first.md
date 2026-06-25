---
name: cuda-first development
description: Future iterations focus exclusively on CUDA GPU rendering, no longer maintaining CPU version parity
type: feedback
---

从 2026-06-25 起，后续所有功能迭代只基于 CUDA 版本开发，不再保持 CPU 版本的同步更新。GPU 核函数和展平逻辑是唯一的渲染路径，CPU 代码仅保留不动作为历史兼容。
