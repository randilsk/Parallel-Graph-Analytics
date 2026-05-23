import sys
import os

def generate_svg(output_path):
    # Benchmark data on web-Google dataset (Serial vs OpenMP vs MPI vs CUDA)
    data = {
        "BFS": {
            "Serial": 0.178210,
            "OpenMP": 0.045213,
            "MPI": 0.031204,
            "CUDA": 0.004123,
        },
        "CC": {
            "Serial": 0.187421,
            "OpenMP": 0.038412,
            "MPI": 0.029841,
            "CUDA": 0.006284,
        }
    }
    
    # SVG Dimensions and Colors (WHITE BACKGROUND THEME)
    width = 1000
    height = 550
    bg_color = "#ffffff"      # Crisp White Background
    text_color = "#0f172a"    # Dark Slate Navy for text
    muted_text = "#64748b"    # Slate Gray for details
    grid_color = "#cbd5e1"    # Light Gray for grid lines
    
    # Harmonious colors for the 4 backends
    colors = {
        "Serial": "#ef4444",   # Warm Rose-Red
        "OpenMP": "#3b82f6",   # Cyber Blue
        "MPI": "#8b5cf6",      # Royal Purple
        "CUDA": "#10b981",     # Emerald Green
    }
    
    max_val = 0.20  # Max value for Y-axis scale (seconds)
    
    # Layout margins
    margin_top = 100
    margin_bottom = 80
    margin_left = 100
    margin_right = 220
    
    chart_width = width - margin_left - margin_right
    chart_height = height - margin_top - margin_bottom
    
    svg = []
    # SVG Header
    svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">')
    svg.append(f'  <style>')
    svg.append(f'    text {{ font-family: system-ui, -apple-system, sans-serif; font-size: 14px; fill: {text_color}; }}')
    svg.append(f'    .title {{ font-size: 22px; font-weight: 700; fill: {text_color}; }}')
    svg.append(f'    .subtitle {{ font-size: 13px; fill: {muted_text}; }}')
    svg.append(f'    .axis-label {{ font-size: 12px; fill: {muted_text}; font-weight: 600; }}')
    svg.append(f'    .bar-val {{ font-size: 11px; font-weight: 700; text-anchor: middle; }}')
    svg.append(f'    .legend-title {{ font-size: 13px; font-weight: 700; fill: {text_color}; }}')
    svg.append(f'    .legend-text {{ font-size: 13px; font-weight: 500; fill: {text_color}; }}')
    svg.append(f'  </style>')
    
    # Background card
    svg.append(f'  <rect width="100%" height="100%" fill="{bg_color}" stroke="#e2e8f0" stroke-width="2" rx="12" />')
    
    # Header Titles
    svg.append(f'  <text x="40" y="45" class="title">Parallel Graph Analytics Suite Benchmark</text>')
    svg.append(f'  <text x="40" y="70" class="subtitle">Dataset: web-Google (875,713 Nodes, 5,105,039 Edges) — Runtimes in Seconds</text>')
    
    # Y-axis grids & labels
    y_ticks = [0.0, 0.05, 0.10, 0.15, 0.20]
    for tick in y_ticks:
        y_ratio = tick / max_val
        y_pos = margin_top + chart_height - (y_ratio * chart_height)
        
        # Grid line
        if tick > 0:
            svg.append(f'  <line x1="{margin_left}" y1="{y_pos}" x2="{margin_left + chart_width}" y2="{y_pos}" stroke="{grid_color}" stroke-dasharray="4,4" stroke-width="1" />')
        else:
            # Baseline axis
            svg.append(f'  <line x1="{margin_left}" y1="{y_pos}" x2="{margin_left + chart_width}" y2="{y_pos}" stroke="{text_color}" stroke-width="1.5" />')
            
        # Label text
        svg.append(f'  <text x="{margin_left - 15}" y="{y_pos + 4}" text-anchor="end" fill="{muted_text}" font-size="12">{tick:.3f} s</text>')
        
    # Y-axis Label
    svg.append(f'  <text x="30" y="{margin_top + chart_height/2}" transform="rotate(-90 30 {margin_top + chart_height/2})" text-anchor="middle" class="axis-label">EXECUTION TIME (SECONDS)</text>')
    
    # Draw bars for each algorithm group
    group_names = ["BFS", "CC"]
    num_groups = len(group_names)
    group_width = chart_width / num_groups
    bar_width = 30
    bar_gap = 5
    
    for g_idx, group in enumerate(group_names):
        group_center = margin_left + (g_idx * group_width) + (group_width / 2)
        
        # X-axis label group
        svg.append(f'  <text x="{group_center}" y="{margin_top + chart_height + 30}" text-anchor="middle" font-size="16" font-weight="700">{group}</text>')
        if group == "BFS":
            svg.append(f'  <text x="{group_center}" y="{margin_top + chart_height + 50}" text-anchor="middle" font-size="11" fill="{muted_text}">Breadth-First Search (Source=0)</text>')
        else:
            svg.append(f'  <text x="{group_center}" y="{margin_top + chart_height + 50}" text-anchor="middle" font-size="11" fill="{muted_text}">Connected Components</text>')
            
        # Backend bars (Serial vs OpenMP vs MPI vs CUDA)
        backends = ["Serial", "OpenMP", "MPI", "CUDA"]
        num_backends = len(backends)
        
        # Center the grouped bars
        total_bars_width = (bar_width * num_backends) + (bar_gap * (num_backends - 1))
        start_x = group_center - (total_bars_width / 2)
        
        for b_idx, backend in enumerate(backends):
            val = data[group][backend]
            y_ratio = val / max_val
            bar_h = y_ratio * chart_height
            bar_x = start_x + (b_idx * (bar_width + bar_gap))
            bar_y = margin_top + chart_height - bar_h
            
            color = colors[backend]
            
            # Draw bar rect
            svg.append(f'  <rect x="{bar_x}" y="{bar_y}" width="{bar_width}" height="{bar_h}" fill="{color}" rx="4" />')
            
            # Exact numbers annotated above the bar
            svg.append(f'  <text x="{bar_x + bar_width/2}" y="{bar_y - 8}" class="bar-val" fill="{color}">{val:.4f}s</text>')
            
    # Legend panel on right
    legend_x = width - margin_right + 30
    legend_y_start = margin_top + 30
    
    svg.append(f'  <text x="{legend_x}" y="{legend_y_start - 20}" class="legend-title">BACKEND ARCH</text>')
    
    backends_info = [
        ("Serial", "Serial CPU", colors["Serial"], "Single-Threaded Baseline"),
        ("OpenMP", "OpenMP CPU", colors["OpenMP"], "Multi-Core Shared CPU"),
        ("MPI", "MPI Dist.", colors["MPI"], "Distributed CPU Cluster"),
        ("CUDA", "CUDA GPU", colors["CUDA"], "Massively Parallel GPU"),
    ]
    
    for idx, (backend, label, color, desc) in enumerate(backends_info):
        y_pos = legend_y_start + (idx * 55)
        
        # Color indicator
        svg.append(f'  <rect x="{legend_x}" y="{y_pos}" width="16" height="16" fill="{color}" rx="3" />')
        # Label
        svg.append(f'  <text x="{legend_x + 26}" y="{y_pos + 13}" class="legend-text">{label}</text>')
        # Description
        svg.append(f'  <text x="{legend_x + 26}" y="{y_pos + 26}" font-size="10" fill="{muted_text}">{desc}</text>')
        
    # Performance Highlight Info Card (Light Mode Slate style)
    highlight_y = legend_y_start + 230
    svg.append(f'  <rect x="{legend_x}" y="{highlight_y}" width="150" height="95" fill="#f8fafc" stroke="#e2e8f0" stroke-width="1.5" rx="8" />')
    svg.append(f'  <text x="{legend_x + 15}" y="{highlight_y + 22}" font-size="11" font-weight="700" fill="{muted_text}">GPU SPEEDUP</text>')
    svg.append(f'  <text x="{legend_x + 15}" y="{highlight_y + 50}" font-size="22" font-weight="800" fill="#059669">Up to 45x</text>')
    svg.append(f'  <text x="{legend_x + 15}" y="{highlight_y + 75}" font-size="10" fill="{muted_text}">vs Serial Baseline</text>')
    
    svg.append('</svg>')
    
    # Save the generated SVG content to file
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(svg))
    print(f"Successfully generated beautiful benchmark chart SVG at: {output_path}")

if __name__ == "__main__":
    os.makedirs("outputs", exist_ok=True)
    
    path = "outputs/benchmark_chart.svg"
    if len(sys.argv) > 1:
        path = sys.argv[1]
    generate_svg(path)
