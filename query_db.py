#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sqlite3
import sys
import os

try:
    db_path = 'arcmeta.db'
    
    # 检查文件是否存在
    if not os.path.exists(db_path):
        print('数据库文件不存在')
        sys.exit(0)
    
    # 以只读模式打开
    conn = sqlite3.connect(f'file:{db_path}?mode=ro', uri=True)
    cursor = conn.cursor()
    
    # 先查询所有现存的表
    cursor.execute("SELECT name FROM sqlite_master WHERE type='table'")
    existing_tables = [row[0] for row in cursor.fetchall()]
    
    print('=' * 70)
    print('数据库数据统计 (arcmeta.db)')
    print('=' * 70)
    
    if not existing_tables:
        print('\n数据库为空，尚未创建任何表。')
        print('需要运行程序进行首次初始化和文件系统扫描。')
    else:
        print(f'\n发现 {len(existing_tables)} 个表:\n')
        total = 0
        for table in existing_tables:
            cursor.execute(f'SELECT COUNT(*) FROM {table}')
            count = cursor.fetchone()[0]
            total += count
            print(f'  {table:25} : {count:12,} 条记录')
        
        print('\n' + '=' * 70)
        print(f'总计数据量                     : {total:12,} 条记录')
        print('=' * 70)
    
    conn.close()
    
except sqlite3.OperationalError as e:
    print(f'数据库被锁定或无法打开: {e}')
    print('程序可能正在运行中，请关闭程序后重试。')
    sys.exit(1)
except Exception as e:
    print(f'错误: {e}')
    import traceback
    traceback.print_exc()
    sys.exit(1)
