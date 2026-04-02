# P1 — 工具系统完善

## 负责团队
核心开发团队 (cqfz-core-develop)

## 阶段目标
完善核心工具实现，补齐 P0 中缺失的重要工具，使工具系统达到生产可用水平。

## P0 已完成（需增强）
- BashTool — 基础执行 ✅，缺少：输出格式化、安全增强、PTY 支持
- FileReadTool — 基础读取 ✅，缺少：图片 base64、多文件批量
- FileWriteTool — 基础写入 ✅，缺少：diff 输出、权限检查增强
- FileEditTool — 精确替换 ✅，缺少：hunk-based diff、冲突检测
- GlobTool — 基于 find ✅，需改用原生实现
- GrepTool — 基于 rg ✅，缺少：context lines、file type 过滤增强

## P1 待开发

### 1. BashTool 增强
- [ ] 输出格式化（区分 stdout/stderr）
- [ ] 环境变量注入（PATH、HOME 等正确传递）
- [ ] 工作目录跟踪（cd 持久化）
- [ ] 安全命令黑名单增强
- [ ] 超时 + SIGTERM/SIGKILL 二段终止

### 2. FileReadTool 增强
- [ ] 图片文件自动 base64 编码
- [ ] 二进制文件类型检测（MIME type）
- [ ] 多文件批量读取

### 3. FileWriteTool 增强
- [ ] 写入后自动生成 unified diff
- [ ] Git diff 集成（如果 git repo 内）
- [ ] 文件时间戳记录

### 4. FileEditTool 增强
- [ ] 并发修改检测（文件在外部被修改时警告）
- [ ] 原子写入（先写临时文件再 rename）
- [ ] Hunk-based diff 输出格式

### 5. GlobTool 重写
- [ ] 原生 C++ glob 实现（不依赖 find 命令）
- [ ] 排除模式支持
- [ ] 结果限制 + 截断通知

### 6. GrepTool 增强
- [ ] Context lines（-C, -A, -B 支持）
- [ ] 文件类型过滤（type:）
- [ ] 搜索结果格式化

### 7. 新增工具
- [ ] `WebFetchTool` — 基于 libcurl 的完整实现
- [ ] `WebSearchTool` — 搜索 API 集成
- [ ] `TodoWriteTool` — 任务列表管理

## 技术要求
- 原子文件写入（write-to-temp + rename）
- 进程管理增强（正确的 SIGTERM -> grace period -> SIGKILL）
- 图片处理（stb_image 或 libmagic 用于 MIME 检测）

## 交付标准
1. 所有 6 个已有工具增强完成
2. 3 个新工具可用
3. 工具 JSON Schema 严格匹配原版
4. 原子写入、并发安全
5. 编译零错误
6. `cmake --build build && ctest` 通过
