# MLP-DPClip 跨数据集实验分析（C=1, sigma=0.01）

## 1. 实验目标

本文档用于记录 Falcon 平台上 `MLP + DPClip` 在不同数据集上的训练效果、通信统计与运行时间统计，并明确模型维度与数据集维度的对应关系，供论文实验分析部分直接引用。

本轮实验固定参数：

- DP 裁剪阈值：`dp_clip_threshold = 1.0`
- DP 噪声强度：`dp_noise_sigma = 0.01`
- 训练轮数：`max_iteration = 100`
- 批大小：`batch_size = 8`
- 优化器：`sgd`
- 学习率：`0.05`
- 正则化：`l2`，`alpha=0.01`

## 2. 实验环境与配置说明

- 系统：WSL/Linux
- 训练模式：Vertical FL（3 方）
- 协调器端口：`31004`（为避免 `30004` 端口冲突，实验使用新端口）
- 参与方端口：`30005/30006/30007`
- MLP-DP DSL：
  - `examples/3party/dsls/examples/train/12.train_mlp_dp_clip.json`（breast）
  - `examples/3party/dsls/examples/train/14.train_mlp_dp_clip_bank.json`（bank）
  - `examples/3party/dsls/examples/train/16.train_mlp_dp_clip_credit.json`（credit）

输出报告文件（active party, client0）：

- `data/dataset/breast_cancer_data/client0/DPClip-MLP-s001-report.txt`
- `data/dataset/bank_marketing_data/client0/DPClip-MLP-s001-report.txt`
- `data/dataset/credit_data/client0/DPClip-MLP-s001-report.txt`

## 3. 数据集维度与模型维度对应关系

### 3.1 原始纵向切分维度（按 client 文件统计）

| 数据集 | client0 行x列 | client1 行x列 | client2 行x列 | 备注 |
|---|---:|---:|---:|---|
| breast_cancer_data | 569 x 11 | 569 x 10 | 569 x 10 | client0 最后一列为标签 |
| bank_marketing_data | 4521 x 7 | 4521 x 5 | 4521 x 5 | client0 最后一列为标签 |
| credit_data | 30000 x 8 | 30000 x 7 | 30000 x 9 | client0 最后一列为标签 |

### 3.2 全局特征维度推导

全局输入维度计算方式：

- 全局输入维度 = (client0 列数 - 1) + client1 列数 + client2 列数

据此得到：

- breast：`(11-1)+10+10 = 30`
- bank：`(7-1)+5+5 = 16`
- credit：`(8-1)+7+9 = 23`

### 3.3 MLP 网络结构与维度映射

| 数据集 | DSL 中 `num_layers_outputs` | 含义 |
|---|---|---|
| breast_cancer_data | `[30, 2, 1]` | 输入30 -> 隐层2 -> 输出1 |
| bank_marketing_data | `[16, 2, 1]` | 输入16 -> 隐层2 -> 输出1 |
| credit_data | `[23, 2, 1]` | 输入23 -> 隐层2 -> 输出1 |

结论：三组实验均保证了“数据特征维度”与“网络输入维度”严格一致。

## 4. 模型训练效果（分类指标）

### 4.1 核心分类结果

| 数据集 | Accuracy | Balanced Accuracy | F1 | TP | TN | FP | FN |
|---|---:|---:|---:|---:|---:|---:|---:|
| breast_cancer_data | 0.9123 | 0.8865 | 0.9351 | 72 | 32 | 8 | 2 |
| bank_marketing_data | 0.9050 | 0.5000 | 0.9501 | 819 | 0 | 86 | 0 |
| credit_data | 0.7857 | 0.5000 | 0.0000 | 0 | 4714 | 0 | 1286 |

### 4.2 结果解读

- breast 数据集表现较好，说明在 `sigma=0.01` 下模型仍具有较好的可学习性。
- bank 与 credit 的 `Balanced Accuracy=0.5`，并出现单类预测塌缩：
  - bank：几乎全预测为 Class1
  - credit：几乎全预测为 Class0
- 这表明在当前网络规模（隐藏层仅 2 个神经元）和训练参数组合下，后两者模型表达能力/优化状态不足，DP 噪声仅为次要因素。

## 5. 运行时间统计

| 数据集 | 训练总时长(s) | Forward(s) | Backward(s) | DPClip(s, 含于Forward) | Eval(s) |
|---|---:|---:|---:|---:|---:|
| breast_cancer_data | 401.672 | 289.184 | 112.335 | 84.666 | 16.533 |
| bank_marketing_data | 363.207 | 267.898 | 94.733 | 78.147 | 139.363 |
| credit_data | 474.205 | 359.843 | 113.750 | 104.879 | 988.685 |

说明：

- `DPClip(s)` 已包含在 `Forward(s)` 中，是其中的子阶段开销。
- credit 的评估阶段开销显著偏大，和数据量（3 万样本）及评估流程中的安全计算通信密切相关。

## 6. 通信统计

### 6.1 各阶段通信总量（active party 报告）

| 数据集 | Forward(B) | Backward(B) | DPClip(B) | Evaluation(B) | Total(B) |
|---|---:|---:|---:|---:|---:|
| breast_cancer_data | 23,545,197 | 33,542,294 | 8,481,083 | 2,670,068 | 59,824,341 |
| bank_marketing_data | 23,545,306 | 22,262,296 | 8,481,157 | 21,197,998 | 67,045,774 |
| credit_data | 23,545,429 | 28,044,532 | 8,481,153 | 140,481,703 | 192,125,940 |

### 6.2 DPClip 通信占比（相对总通信）

计算公式：

- DPClip 占比 = DPClip 通信 / Total 通信

近似结果：

- breast：`8,481,083 / 59,824,341 ≈ 14.18%`
- bank：`8,481,157 / 67,045,774 ≈ 12.65%`
- credit：`8,481,153 / 192,125,940 ≈ 4.41%`

解读：

- DPClip 绝对通信量在三组任务中较接近（都在约 8.48MB），说明该模块开销更多由 batch 级协议流程决定。
- 随着整体任务通信增大（尤其 credit 的评估通信激增），DPClip 相对占比会明显下降。

## 7. 关键结论（可直接用于论文）

1. 在固定 `C=1, sigma=0.01` 条件下，MLP-DP 在 breast_cancer 数据集保持了较高精度（Accuracy 0.912, F1 0.935）。
2. 在 bank 与 credit 数据集上，模型出现类别塌缩（Balanced Accuracy 0.5），主要反映当前网络结构规模较小（仅 2 个隐层神经元）及任务难度/类别分布对优化的影响。
3. DPClip 的通信开销在本实验设置中稳定在约 `8.48MB`，其相对占比随总通信体量增大而下降（14.18% -> 4.41%）。
4. credit 数据集评估阶段通信与时间明显高于其余数据集，是整体开销差异的主要来源。

## 8. 复现实验命令（端口已切换）

```bash
# 清理
pkill -9 -f falcon_platform 2>/dev/null
pkill -9 -f semi-party 2>/dev/null
pkill -9 -f "executor/falcon" 2>/dev/null
lsof -ti :30001-31007 2>/dev/null | xargs -r kill -9
sleep 1

# 启动 coordinator + parties
cd /home/merak/falcon
nohup bash examples/3party/coordinator/debug_coord.sh > /tmp/coord.log 2>&1 &
for i in 0 1 2; do
  nohup bash examples/3party/party${i}/debug_partyserver.sh -partyID $i > /tmp/party${i}.log 2>&1 &
done

# 提交三个任务（使用 31004）
python3 examples/coordinator_client.py -url 127.0.0.1:31004 -method submit -path examples/3party/dsls/examples/train/12.train_mlp_dp_clip.json
python3 examples/coordinator_client.py -url 127.0.0.1:31004 -method submit -path examples/3party/dsls/examples/train/14.train_mlp_dp_clip_bank.json
python3 examples/coordinator_client.py -url 127.0.0.1:31004 -method submit -path examples/3party/dsls/examples/train/16.train_mlp_dp_clip_credit.json
```

## 9. 备注

- 本文档为 `MLP-DPClip` 单组参数（`C=1, sigma=0.01`）的跨数据集结果汇总。
- 若论文需要“隐私-效用权衡曲线”，建议在固定 `C=1` 下继续扫描 `sigma`（如 `0.001/0.01/0.05/0.1`），并补充与 non-DP MLP 的并列表格。
