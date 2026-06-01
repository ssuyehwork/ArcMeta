import sqlite3
import os
import sys

def run_diagnostic():
    db_path = 'arcmeta.db'
    print(f"--- ArcMeta 数据库深度诊断 [%s] ---" % db_path)
    
    if not os.path.exists(db_path):
        print(f"错误: 数据库文件不存在。请确认主程序已启动并完成了初始化。")
        return
    
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        # 1. 检查表结构
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table';")
        tables = [t[0] for t in cursor.fetchall()]
        print(f"已发现表: {tables}")
        
        if 'items' not in tables:
            print("致命错误: 'items' 表缺失，初始化可能失败。")
            return
            
        # 2. 检查数据总量
        cursor.execute("SELECT count(*) FROM items")
        total = cursor.fetchone()[0]
        print(f"数据总行数: {total}")
        
        if total == 0:
            print("警告: 'items' 表为空。可能原因：MFT 扫描尚未执行或入库逻辑失效。")
        else:
            # 3. 检查字段质量
            cursor.execute("SELECT volume, frn, path, file_id_128 FROM items LIMIT 5")
            rows = cursor.fetchall()
            print("\n数据样本 (前5条):")
            print("-" * 50)
            print("Volume | FRN | Path | FileID")
            for row in rows:
                print(f"{row[0]} | {row[1]} | {row[2][:30]}... | {row[3][:20]}...")
            
            # 4. 检查标识符格式
            cursor.execute("SELECT frn FROM items LIMIT 10")
            frns = [r[0] for r in cursor.fetchall()]
            hex_count = sum(1 for f in frns if all(c in '0123456789ABCDEF' for c in f.upper()))
            print(f"\n标识符校验:")
            print(f"- 十六进制 FRN 比例: {hex_count/len(frns)*100:.1f}%")
            
            cursor.execute("SELECT count(*) FROM items WHERE path IS NULL OR path = ''")
            empty_paths = cursor.fetchone()[0]
            print(f"- 路径缺失行数: {empty_paths}")
            
        conn.close()
    except Exception as e:
        print(f"执行诊断时发生异常: {e}")

if __name__ == "__main__":
    run_diagnostic()
