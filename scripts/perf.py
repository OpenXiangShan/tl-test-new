import os
import re
import subprocess
import matplotlib.pyplot as plt
import csv

# 定义文件路径
FUZZER_H_PATH = "main/Fuzzer/Fuzzer.h"
TESTTOP_SCALA_PATH = "dut/OpenLLC/src/test/scala/TestTop.scala"
EXECUTABLE = "make run"  # 替换为你的可执行文件路径

# 修改指定行内容的函数
def modify_line(file_path, line_number, new_content):
    """
    修改文件中指定行的内容。
    
    :param file_path: 文件路径
    :param line_number: 要修改的行号（从 1 开始计数）
    :param new_content: 新的内容（不带换行符）
    """
    with open(file_path, "r") as file:
        lines = file.readlines()

    # 修改指定行
    if 1 <= line_number <= len(lines):
        lines[line_number - 1] = new_content + "\n"

    # 写回文件
    with open(file_path, "w") as file:
        file.writelines(lines)

def modify_blk(blkCountLimit):
    # 使用行号精确修改代码
    modify_line("main/Fuzzer/Fuzzer.h", 161, f"uint32_t blkCountLimit = {blkCountLimit};")

def modify_banks(banks):
    modify_line("dut/OpenLLC/src/test/scala/TestTop.scala", 275, f"banks = {banks}")

# 编译代码的函数
def compile_code_pre():
    try:
        commands = [
            "make openLLC-verilog-test-top-l2l3",
            "make THREADS_BUILD=128 openLLC-verilate",
        ]
        for command in commands:
            print(f"Running: {command}")
            subprocess.run(command, shell=True, check=True)
    except subprocess.CalledProcessError:
        print("Compilation failed.")
        exit(1)

# 编译代码的函数
def compile_code_fmt():
    try:
        commands = [
            "make THREADS_BUILD=128 tltest-config-openLLC-test-l2l3",
            "make THREADS_BUILD=128 tltest-build-all-openLLC",
        ]
        for command in commands:
            print(f"Running: {command}")
            subprocess.run(command, shell=True, check=True)
    except subprocess.CalledProcessError:
        print("Compilation failed.")
        exit(1)

# 运行仿真并获取输出
def run_simulation():
    try:
        print("Running simulation: make run")
        result = subprocess.run(
            ["make run"], stdout=subprocess.PIPE, text=True, shell=True, check=True
        )
        output = result.stdout
        # 提取 perf debug 的值
        match = re.search(r"perf debug : ([\d.]+)B/Cycle", output)
        if match:
            return float(match.group(1))
        else:
            print("perf debug output not found.")
            return None
    except subprocess.CalledProcessError:
        print("Execution failed.")
        return None

# 主函数
def main():
    results = []
    blkCountLimits = [16, 32, 64, 512]
    banks_values = [1, 2, 4, 8]

    # 遍历参数组合
    for banks in banks_values:  # banks 外循环
        modify_banks(banks)
        compile_code_pre()
        for blkCountLimit in blkCountLimits:
            print(f"Testing blkCountLimit={blkCountLimit}, banks={banks}...")
            modify_blk(blkCountLimit)
            compile_code_fmt()
            perf_debug = run_simulation()
            if perf_debug is not None:
                results.append((blkCountLimit, banks, perf_debug))
            print(f"Result: blkCountLimit={blkCountLimit}, banks={banks}, perf_debug={perf_debug}")

    # 保存数据为 CSV
    save_results_to_csv(results, "performance_results.csv")

    # 可视化
    visualize_results(results, blkCountLimits, banks_values)

# 保存结果为 CSV
def save_results_to_csv(results, filename):
    with open(filename, mode="w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["blkCountLimit", "banks", "perf_debug (B/Cycle)"])
        writer.writerows(results)
    print(f"Results saved to {filename}")

# 可视化函数
def visualize_results(results, blkCountLimits, banks_values):
    for banks in banks_values:
        values = [
            next((r[2] for r in results if r[0] == blkCountLimit and r[1] == banks), 0)
            for blkCountLimit in blkCountLimits
        ]
        plt.plot(blkCountLimits, values, label=f"banks={banks}")

    plt.xlabel("blkCountLimit")
    plt.ylabel("perf debug (B/Cycle)")
    plt.title("Performance Debug vs blkCountLimit and banks")
    plt.legend()
    plt.grid(True)

    # 保存图像到文件
    plt.savefig("performance_plot.png")
    print("Plot saved as performance_plot.png")
    plt.close()

if __name__ == "__main__":
    main()
