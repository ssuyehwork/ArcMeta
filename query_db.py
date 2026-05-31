import sqlite3
import os

def check_db():
    db_path = 'arcmeta.db'
    if not os.path.exists(db_path):
        print(f"Database file {db_path} not found.")
        return
    
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        # Check tables
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table';")
        tables = cursor.fetchall()
        print(f"Tables: {tables}")

        if ('items',) in tables:
            cursor.execute("SELECT count(*) FROM items")
            count = cursor.fetchone()[0]
            print(f"Number of rows in 'items' table: {count}")

            if count > 0:
                cursor.execute("SELECT volume, frn, path FROM items LIMIT 5")
                rows = cursor.fetchall()
                print("First 5 rows in 'items':")
                for row in rows:
                    print(row)
        else:
            print("'items' table not found.")

        conn.close()
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    check_db()
