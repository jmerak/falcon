### 清楚所有终端：
pkill -9 -f falcon_platform 2>/dev/null; pkill -9 -f semi-party 2>/dev/null; pkill -9 -f "executor/falcon" 2>/dev/null; lsof -ti :30001-31007 2>/dev/null | xargs kill -9 2>/dev/null; sleep 1; echo "cleaned"

### 删除所有日志：
rm -f /home/merak/falcon/data/dataset/breast_cancer_data/client*/DPClip-MLP-report.txt /home/merak/falcon/data/dataset/breast_cancer_data/client*/DPClip-MLP-model.pb 2>/dev/null; echo "old outputs removed"

### 启动协调器
cd /home/merak/falcon && nohup bash examples/3party/coordinator/debug_coord.sh > /tmp/coord.log 2>&1 & sleep 3; grep -q "listening" /tmp/coord.log && echo "COORD OK" || tail -5 /tmp/coord.log

### 启动三方
cd /home/merak/falcon && for i in 0 1 2; do nohup bash examples/3party/party${i}/debug_partyserver.sh -partyID $i > /tmp/party${i}.log 2>&1 & done; sleep 3; for i in 0 1 2; do grep -q "listening" /tmp/party${i}.log && echo "Party $i OK" || tail -3 /tmp/party${i}.log; done


### 提交任务，返回任务 ID
cd /home/merak/falcon && python3 examples/coordinator_client.py -url 127.0.0.1:31004 -method submit -path examples/3party/dsls/examples/train/12.train_mlp_dp_clip.json 2>&1

### 查询任务进度
cd /home/merak/falcon && for attempt in $(seq 1 200); do sleep 15; status=$(python3 examples/coordinator_client.py -url 127.0.0.1:31004 -method query_status -job 1 2>&1); echo "[$(date +%H:%M:%S)] $status"; if [ "$status" = "finished" ] || [ "$status" = "failed" ]; then break; fi; done

### 查看任务报告
cat /home/merak/falcon/data/dataset/breast_cancer_data/client0/DPClip-MLP-report.txt
