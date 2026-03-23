# Falcon 项目运行指南

> 本指南基于 WSL2 Ubuntu 20.04 + subprocess 模式，适用于本地单机 3 方联邦学习。
> 假设环境已准备完毕（Docker、Go 1.14、gcc/g++、依赖库均已安装）。

---

## 目录

1. [项目结构概览](#1-项目结构概览)
2. [启动服务](#2-启动服务)
3. [提交训练任务](#3-提交训练任务)
4. [查看训练结果](#4-查看训练结果)
5. [关闭服务与正常关机](#5-关闭服务与正常关机)
6. [重新启动](#6-重新启动)
7. [常用命令速查](#7-常用命令速查)
8. [可用数据集与模型](#8-可用数据集与模型)
9. [常见问题排查](#9-常见问题排查)

---

## 1. 项目结构概览

```
/home/merak/falcon/                  # 项目根目录（也是 /opt/falcon 的符号链接目标）
├── build/src/executor/falcon        # C++ 执行器二进制文件
├── src/falcon_platform/             # Go 平台代码（coordinator + partyserver）
├── data/dataset/                    # 数据集目录
├── examples/3party/                 # 3 方运行配置与 DSL 文件
│   ├── coordinator/                 # Coordinator 启动脚本与配置
│   ├── party0/                      # Party 0 (Active) 启动脚本与配置
│   ├── party1/                      # Party 1 (Passive) 启动脚本与配置
│   ├── party2/                      # Party 2 (Passive) 启动脚本与配置
│   └── dsls/                        # DSL 任务定义文件
├── falcon_logs/                     # 运行日志（自动生成）
├── lib_deps/                        # 共享库依赖
└── third_party/MP-SPDZ/             # MPC 引擎
```

**关键端口：**
| 服务 | 端口 |
|------|------|
| Coordinator | 30004 |
| Party 0 | 30005 |
| Party 1 | 30006 |
| Party 2 | 30007 |

---

## 2. 启动服务

需要 **4 个独立终端**，按顺序启动。

### 2.1 启动 Coordinator（终端 1）

```bash
cd /home/merak/falcon
export GOROOT=$HOME/go
export GOPATH=$HOME/gopath
export PATH=$GOROOT/bin:$GOPATH/bin:$HOME/.local/bin:$PATH
bash examples/3party/coordinator/debug_coord.sh
```

等待输出类似 `listening on 127.0.0.1:30004` 后再继续。

### 2.2 启动 Party 0（终端 2）

```bash
cd /home/merak/falcon
export GOROOT=$HOME/go
export GOPATH=$HOME/gopath
export PATH=$GOROOT/bin:$GOPATH/bin:$HOME/.local/bin:$PATH
bash examples/3party/party0/debug_partyserver.sh --partyID 0
```

### 2.3 启动 Party 1（终端 3）

```bash
cd /home/merak/falcon
export GOROOT=$HOME/go
export GOPATH=$HOME/gopath
export PATH=$GOROOT/bin:$GOPATH/bin:$HOME/.local/bin:$PATH
bash examples/3party/party1/debug_partyserver.sh --partyID 1
```

### 2.4 启动 Party 2（终端 4）

```bash
cd /home/merak/falcon
export GOROOT=$HOME/go
export GOPATH=$HOME/gopath
export PATH=$GOROOT/bin:$GOPATH/bin:$HOME/.local/bin:$PATH
bash examples/3party/party2/debug_partyserver.sh --partyID 2
```

> **验证：** 4 个终端都显示运行中、无报错，即表示服务启动成功。

---

## 3. 提交训练任务

在 **第 5 个终端**（操作终端）中执行。

### 3.1 提交单个任务

```bash
cd /home/merak/falcon

# 提交 Logistic Regression 训练（breast_cancer 数据集）
python3 examples/coordinator_client.py \
  -url 127.0.0.1:30004 \
  -method submit \
  -path examples/3party/dsls/examples/train/8.train_logistic_reg.json

# 返回 Job ID（例如：1）
```

### 3.2 查询任务状态

```bash
# 将 <job_id> 替换为实际的 Job ID
python3 examples/coordinator_client.py \
  -url 127.0.0.1:30004 \
  -method query_status \
  -job <job_id>

# 返回值：finished / failed / 其他（运行中）
```

### 3.3 停止正在运行的任务

```bash
python3 examples/coordinator_client.py \
  -url 127.0.0.1:30004 \
  -method kill \
  -job <job_id>
```

### 3.4 批量提交任务

```bash
cd /home/merak/falcon/examples
bash execute_dsls.sh 3party/dsls/examples/train
# 会依次提交目录下所有 DSL 文件，前一个完成后才提交下一个
```

### 3.5 可用的 DSL 文件

```bash
# 集中式训练（基础示例）
examples/3party/dsls/examples/train/
├── 6.train_dt.json                  # 决策树
├── 7.train_linear_reg.json          # 线性回归
├── 8.train_logistic_reg.json        # 逻辑回归 ✅ 已验证
├── 9.train_rf.json                  # 随机森林
├── 10.train_gbdt.json               # GBDT
└── 11.train_mlp.json                # MLP 神经网络 ✅ 已验证

# 更多数据集的训练配置
examples/3party/dsls/dsls_cent/
├── 4.train_logistic_l1_bank.json    # 逻辑回归 L1 + 银行营销
├── 5.train_logistic_l2_credit.json  # 逻辑回归 L2 + 信用卡
├── 2.train_linear_l1_bike.json      # 线性回归 L1 + 共享单车
├── 8.train_mlp_l2_relu_*.json       # MLP ReLU 各数据集
└── ...
```

---

## 4. 查看训练结果

### 4.1 查看报告文件（推荐）

报告路径 = DSL 中的 `model_path` + `output_configs.evaluation_report`。

```bash
# Logistic Regression 报告
cat /opt/falcon/data/dataset/breast_cancer_data/client0/report.txt

# MLP 报告
cat /opt/falcon/data/dataset/breast_cancer_data/client0/20220611-MLP-report.txt
```

> 只有 Party 0 (Active Party) 的报告包含评估指标（准确率、F1 等）。
> 所有 Party 的报告都包含通信统计。

### 4.2 查看运行日志

```bash
# 列出所有日志目录
ls falcon_logs/

# 查看最新一次 Party 0 的训练日志
LATEST=$(ls -td falcon_logs/Party-0_* | head -1)
ls $LATEST/runtime_logs/

# 查看具体任务日志
cat $LATEST/runtime_logs/job-<id>-train-<algorithm>/centralized_worker/falcon.INFO
```

### 4.3 通过 API 查询状态

```bash
python3 examples/coordinator_client.py \
  -url 127.0.0.1:30004 \
  -method query_status \
  -job <job_id>
```

---

## 5. 关闭服务与正常关机

### 5.1 关闭 Falcon 服务

在运行服务的 4 个终端中，分别按 `Ctrl+C` 终止进程。  
**建议关闭顺序：** Party 2 → Party 1 → Party 0 → Coordinator（先关 Party 再关 Coordinator）。

或者在操作终端中一次性关闭所有进程：

```bash
# 方法一：通过进程名杀掉所有 falcon_platform 进程
pkill -f falcon_platform

# 方法二：精确杀掉（先确认 PID）
ps aux | grep falcon_platform | grep -v grep
kill <pid1> <pid2> <pid3> <pid4>
```

### 5.2 验证服务已停止

```bash
ps aux | grep falcon_platform | grep -v grep
# 应该没有任何输出
```

### 5.3 关于 Docker Swarm

本项目初始化了 Docker Swarm（即使 subprocess 模式也需要）。  
**日常关机不需要关闭 Docker Swarm**，它随 Docker 服务自动启停。

```bash
# 查看 Swarm 状态
docker node ls

# ⚠️ 如需彻底移除 Swarm（通常不需要）
docker swarm leave --force
# 下次启动需要重新初始化：
# docker swarm init --advertise-addr 127.0.0.1
# docker node update --label-add name=host $(docker node ls -q)
```

### 5.4 清理 Docker 资源（可选）

```bash
# 清理已停止的容器（如有）
docker container prune -f

# 清理未使用的镜像（慎用）
docker image prune -f
```

### 5.5 完整关机流程

```bash
# 1. 终止所有 Falcon 服务
pkill -f falcon_platform

# 2. 确认已停止
ps aux | grep falcon_platform | grep -v grep

# 3. （可选）停止 Docker 服务
sudo systemctl stop docker

# 4. 现在可以安全关机或关闭 WSL
```

---

## 6. 重新启动

### 6.1 快速重启（下次使用直接执行）

```bash
# 确保 Docker 在运行
sudo systemctl start docker

# 确认 Swarm 正常
docker node ls
# 如果报错，重新初始化：
# docker swarm init --advertise-addr 127.0.0.1
# docker node update --label-add name=host $(docker node ls -q)

# 然后按 "第 2 节" 的步骤依次启动 4 个服务
```

### 6.2 重新编译（修改代码后）

```bash
# 编译 C++ 执行器
docker run --rm --entrypoint bash \
  -v /home/merak/falcon:/workspace \
  lemonwyc/falcon-pub:Dec2023 \
  -c "cd /workspace/build && make -j4"

# 修复权限
sudo chown -R merak:merak /home/merak/falcon/build/src/executor/falcon

# Go 平台会在启动脚本中自动编译，无需手动处理
```

---

## 7. 常用命令速查

```bash
# ========== 服务管理 ==========
# 启动（4 个终端分别执行，见第 2 节）
# 停止所有服务
pkill -f falcon_platform

# ========== 任务管理 ==========
# 提交任务
python3 examples/coordinator_client.py -url 127.0.0.1:30004 -method submit -path <dsl.json>

# 查询状态
python3 examples/coordinator_client.py -url 127.0.0.1:30004 -method query_status -job <id>

# 停止任务
python3 examples/coordinator_client.py -url 127.0.0.1:30004 -method kill -job <id>

# ========== 查看结果 ==========
# 查看报告
cat /opt/falcon/data/dataset/<dataset>/client0/<report_file>

# 查看最新日志
ls -t falcon_logs/Party-0_* | head -1

# ========== 编译 ==========
# 编译 executor
docker run --rm --entrypoint bash -v /home/merak/falcon:/workspace \
  lemonwyc/falcon-pub:Dec2023 -c "cd /workspace/build && make -j4"

# ========== Docker 状态 ==========
docker node ls
docker service ls
ps aux | grep falcon_platform | grep -v grep
```

---

## 8. 可用数据集与模型

### 数据集

| 数据集 | 类型 | 目录 |
|--------|------|------|
| breast_cancer | 二分类 | `data/dataset/breast_cancer_data/` |
| bank_marketing | 二分类 | `data/dataset/bank_marketing_data/` |
| credit | 二分类 | `data/dataset/credit_data/` |
| connect4 | 多分类 | `data/dataset/connect4_data/` |
| energy_prediction | 回归 | `data/dataset/energy_prediction_data/` |
| bike_new | 回归 | `data/dataset/bike_new/` |
| Bike-Sharing | 回归 | `data/dataset/Bike-Sharing-Dataset/` |
| news_popularity | 回归 | `data/dataset/news_popularity_data/` |

### 模型

| 算法 | DSL 名称 | 分布式 | 适用任务 |
|------|----------|--------|----------|
| Logistic Regression | `logistic_regression` | ✅ | 分类 |
| Linear Regression | `linear_regression` | ✅ | 回归 |
| Decision Tree | `decision_tree` | ✅ | 分类/回归 |
| Random Forest | `random_forest` | ❌ | 分类/回归 |
| GBDT | `gbdt` | ❌ | 分类/回归 |
| MLP | `mlp` | ✅ | 分类/回归 |

---

## 9. 常见问题排查

### Q: 服务无法启动，端口被占用
```bash
# 查看端口占用
lsof -i :30004
lsof -i :30005
# 杀掉占用进程后重试
```

### Q: 训练任务状态一直不是 finished
```bash
# 查看最新的 Party 0 日志排查错误
LATEST=$(ls -td falcon_logs/Party-0_* | head -1)
tail -50 $LATEST/runtime_logs/job-*-*/centralized_worker/falcon.INFO
```

### Q: Docker Swarm 异常
```bash
docker swarm leave --force
docker swarm init --advertise-addr 127.0.0.1
docker node update --label-add name=host $(docker node ls -q)
```

### Q: 编译报错（修改代码后）
```bash
# 完整重新编译
docker run --rm --entrypoint bash \
  -v /home/merak/falcon:/workspace \
  lemonwyc/falcon-pub:Dec2023 \
  -c "cd /workspace && cmake -Bbuild -H. && cd build && make -j4"
```

### Q: MP-SPDZ 证书过期
```bash
docker run --rm --entrypoint bash \
  -v /home/merak/falcon:/workspace \
  lemonwyc/falcon-pub:Dec2023 \
  -c "cd /opt/falcon/third_party/MP-SPDZ && Scripts/setup-ssl.sh 3 128 128 && c_rehash Player-Data/ && cp -r Player-Data /workspace/third_party/MP-SPDZ/"
```
