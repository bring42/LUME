#!/usr/bin/env python3
"""
Automatically gzip web files before uploading to ESP32 filesystem.
ESPAsyncWebServer will serve .gz files automatically if they exist.
"""
Import("env")
import gzip
import shutil
from pathlib import Path

def gzip_web_files(source, target, env):
    """Compress HTML, CSS, and JS files in data/ directory"""
    data_dir = Path("data")
    
    if not data_dir.exists():
        print("⚠️  data/ directory not found, skipping gzip")
        return
    
    # File extensions to compress
    extensions = {".html", ".css", ".js", ".json", ".svg", ".xml"}
    
    files_compressed = 0
    total_saved = 0
    
    for file_path in data_dir.rglob("*"):
        if file_path.is_file() and file_path.suffix in extensions:
            gz_path = file_path.with_suffix(file_path.suffix + ".gz")
            
            # Skip if .gz is newer than source
            if gz_path.exists() and gz_path.stat().st_mtime > file_path.stat().st_mtime:
                continue
            
            original_size = file_path.stat().st_size
            
            with open(file_path, 'rb') as f_in:
                with gzip.open(gz_path, 'wb', compresslevel=9) as f_out:
                    shutil.copyfileobj(f_in, f_out)
            
            compressed_size = gz_path.stat().st_size
            saved = original_size - compressed_size
            total_saved += saved
            files_compressed += 1
            
            percent = (saved / original_size * 100) if original_size > 0 else 0
            print(f"✓ {file_path.name}: {original_size:,} → {compressed_size:,} bytes (-{percent:.0f}%)")
    
    if files_compressed > 0:
        print(f"📦 Compressed {files_compressed} files, saved {total_saved:,} bytes total")
    else:
        print("✓ All files already compressed")

# Register the callback to run before uploadfs
env.AddPreAction("uploadfs", gzip_web_files)
env.AddPreAction("uploadfsota", gzip_web_files)
