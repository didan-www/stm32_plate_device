#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
车牌识别系统服务端

运行：
  pip install flask paho-mqtt
  python server.py

访问：
  http://localhost:8089/
"""

import os, json, time, threading
from datetime import datetime
from flask import Flask, request, send_from_directory, render_template_string

UPLOAD_DIR = "uploads"
HTTP_PORT  = 8089  #根据自己的情况更改即可

app = Flask(__name__)
os.makedirs(UPLOAD_DIR, exist_ok=True)


@app.route("/api/v1/plate/upload", methods=["POST"])
def upload():
    """
    分块上传接口

    请求头：
      X-Device-Id     设备标识
      X-Img-Id        图片编号
      X-Chunk-Offset  本块在文件中的起始偏移
      X-Total-Size    文件总大小

    响应：
      未收齐 -> "PARTIAL"
      已收齐 -> "COMPLETE"
    """
    dev_id = request.headers.get("X-Device-Id", "unknown")
    img_id = request.headers.get("X-Img-Id", "0")
    offset = int(request.headers.get("X-Chunk-Offset", "0"))
    total  = int(request.headers.get("X-Total-Size", "0"))

    data = request.get_data()
    if not data:
        return "NO DATA", 400

    # 防御：offset=0 的块必须以 JPEG SOI 开头
    if offset == 0 and data[:2] != b'\xff\xd8':
        print(f"[UP] reject: first chunk not JPEG SOI (got {data[:4].hex()})")
        return "INVALID SOI", 400

    filename = f"{dev_id}_{img_id.zfill(3)}.jpg"
    path = os.path.join(UPLOAD_DIR, filename)

    # 按偏移写入。offset=0 时新建，否则追加到指定位置
    mode = "r+b" if (offset > 0 and os.path.exists(path)) else "wb"
    with open(path, mode) as f:
        f.seek(offset)
        f.write(data)

    received = offset + len(data)
    print(f"[UP] {filename} {received}/{total} bytes")

    if total > 0 and received >= total:
        with open(path, "rb") as f:
            content = f.read()

        # 校验 JPEG 完整性
        if content[:2] == b"\xff\xd8" and content[-2:] == b"\xff\xd9":
            print(f"[UP] {filename} COMPLETE, valid JPEG")
            threading.Thread(target=process_image,
                             args=(dev_id, img_id, path), daemon=True).start()
            return "COMPLETE", 200
        else:
            print(f"[UP] {filename} invalid JPEG "
                  f"(head={content[:2].hex()}, tail={content[-2:].hex()})")
            return "INVALID JPEG", 200

    return "PARTIAL", 200


def process_image(dev_id, img_id, path):
    """图片收齐后的处理：识别 + MQTT 下发"""
    print(f"[PROC] recognizing {path}")
    # TODO: 接入 OpenCV 车牌识别
    # TODO: MQTT 下发结果


PAGE = """
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>车牌识别系统 - 图片</title>
<style>
  :root { --bg:#0f1115; --card:#1a1d24; --border:#2a2f3a;
          --text:#e4e6eb; --muted:#8b919e; --accent:#4a9eff; }
  * { box-sizing:border-box; margin:0; padding:0; }
  body { background:var(--bg); color:var(--text); padding:24px;
         font:14px/1.6 -apple-system,"Segoe UI","Microsoft YaHei",sans-serif; }
  header { display:flex; align-items:baseline; gap:16px; margin-bottom:24px;
           padding-bottom:16px; border-bottom:1px solid var(--border); }
  h1 { font-size:20px; font-weight:600; }
  .count { color:var(--muted); font-size:13px; }
  .refresh { margin-left:auto; padding:6px 14px; background:var(--card);
             border:1px solid var(--border); color:var(--text);
             border-radius:6px; cursor:pointer; font-size:13px; }
  .refresh:hover { border-color:var(--accent); }
  .grid { display:grid; gap:16px;
          grid-template-columns:repeat(auto-fill,minmax(280px,1fr)); }
  .card { background:var(--card); border:1px solid var(--border);
          border-radius:8px; overflow:hidden; }
  .card img { width:100%; display:block; background:#000;
              aspect-ratio:4/3; object-fit:contain; }
  .info { padding:12px 14px; }
  .name { font-weight:500; margin-bottom:6px; word-break:break-all; }
  .meta { color:var(--muted); font-size:12px; }
  .empty { text-align:center; color:var(--muted); padding:60px 20px; }
</style>
</head>
<body>
  <header>
    <h1>车牌识别系统</h1>
    <span class="count">共 {{ images|length }} 张</span>
    <button class="refresh" onclick="location.reload()">刷新</button>
  </header>
  {% if images %}
  <div class="grid">
    {% for img in images %}
    <div class="card">
      <img src="/uploads/{{ img.name }}" loading="lazy">
      <div class="info">
        <div class="name">{{ img.name }}</div>
        <div class="meta">{{ img.size }} · {{ img.time }}</div>
      </div>
    </div>
    {% endfor %}
  </div>
  {% else %}
  <div class="empty">暂无图片</div>
  {% endif %}
</body>
</html>
"""


@app.route("/")
def index():
    images = []
    for name in sorted(os.listdir(UPLOAD_DIR), reverse=True):
        if not name.lower().endswith(".jpg"):
            continue
        stat = os.stat(os.path.join(UPLOAD_DIR, name))
        images.append({
            "name": name,
            "size": f"{stat.st_size / 1024:.1f} KB",
            "time": datetime.fromtimestamp(stat.st_mtime).strftime("%m-%d %H:%M:%S"),
        })
    return render_template_string(PAGE, images=images)


@app.route("/uploads/<path:filename>")
def serve_upload(filename):
    return send_from_directory(UPLOAD_DIR, filename)


if __name__ == "__main__":
    print(f"[HTTP] listening on 0.0.0.0:{HTTP_PORT}")
    print(f"[HTTP] open http://localhost:{HTTP_PORT}/ to view images")
    app.run(host="0.0.0.0", port=HTTP_PORT, threaded=True)
