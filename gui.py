import sys
import subprocess
import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, font

class ModernCompilerGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Mini Compiler IDE - [Flex & Bison Backend]")
        # Fix: Removed spaces around 'x' in geometry specification
        self.root.geometry("1100x700")
        self.root.configure(bg="#1e1e1e")

        # Custom Fonts
        self.code_font = font.Font(family="Consolas", size=11)
        self.ui_font = font.Font(family="Segoe UI", size=10)

        self.setup_ui()

    def setup_ui(self):
        # 1. Top Header / Action Bar
        header_frame = tk.Frame(self.root, bg="#2d2d2d", height=50)
        header_frame.pack(fill=tk.X, side=tk.TOP)

        title_label = tk.Label(
            header_frame, 
            text="⚡ MiniCompiler IDE", 
            fg="#007acc", 
            bg="#2d2d2d", 
            font=("Segoe UI", 12, "bold")
        )
        title_label.pack(side=tk.LEFT, padx=15, pady=10)

        # Buttons
        btn_style = {"font": ("Segoe UI", 9, "bold"), "relief": "flat", "padx": 15, "pady": 5, "cursor": "hand2"}

        self.btn_compile = tk.Button(
            header_frame, text="▶ COMPILE & GENERATE TAC", bg="#0e639c", fg="white",
            activebackground="#1177bb", activeforeground="white", command=self.run_compiler, **btn_style
        )
        self.btn_compile.pack(side=tk.LEFT, padx=10, pady=8)

        self.btn_load = tk.Button(
            header_frame, text="📂 Open File", bg="#3c3c3c", fg="white",
            activebackground="#505050", activeforeground="white", command=self.load_file, **btn_style
        )
        self.btn_load.pack(side=tk.LEFT, padx=5, pady=8)

        self.btn_sample = tk.Button(
            header_frame, text="✨ Load Sample Code", bg="#3c3c3c", fg="white",
            activebackground="#505050", activeforeground="white", command=self.load_sample, **btn_style
        )
        self.btn_sample.pack(side=tk.LEFT, padx=5, pady=8)

        # 2. Main Workspace (PanedWindow for split view)
        paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        # Style for PanedWindow sash
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('Sash', sashthickness=5, background="#2d2d2d")

        # Left Panel - Code Editor
        left_frame = tk.Frame(paned, bg="#252526")
        paned.add(left_frame, weight=1)

        editor_label = tk.Label(left_frame, text="SOURCE CODE (.mc / .txt)", fg="#cccccc", bg="#252526", font=self.ui_font, anchor="w")
        editor_label.pack(fill=tk.X, padx=10, pady=5)

        self.editor = tk.Text(
            left_frame, bg="#1e1e1e", fg="#d4d4d4", insertbackground="white",
            font=self.code_font, relief="flat", padx=10, pady=10, undo=True
        )
        self.editor.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # Right Panel - Compiler Terminal Output
        right_frame = tk.Frame(paned, bg="#252526")
        paned.add(right_frame, weight=1)

        output_label = tk.Label(right_frame, text="COMPILER OUTPUT (AST / Semantics / TAC)", fg="#cccccc", bg="#252526", font=self.ui_font, anchor="w")
        output_label.pack(fill=tk.X, padx=10, pady=5)

        self.output = tk.Text(
            right_frame, bg="#000000", fg="#4ec9b0", insertbackground="white",
            font=self.code_font, relief="flat", padx=10, pady=10
        )
        self.output.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # 3. Status Bar
        self.status_bar = tk.Label(self.root, text="Ready", fg="#888888", bg="#007acc", font=("Segoe UI", 9), anchor="w", padx=10)
        self.status_bar.pack(fill=tk.X, side=tk.BOTTOM)

    def load_sample(self):
        sample_code = """int x;
int y;
bool flag;

x = 10;
y = 0;
flag = true;

while (x > 0) {
    y = y + x;
    x = x - 1;
}

if (flag == true) {
    print y;
} else {
    print x;
}"""
        self.editor.delete("1.0", tk.END)
        self.editor.insert(tk.END, sample_code)
        self.status_bar.config(text="Sample code loaded successfully.", bg="#007acc")

    def load_file(self):
        file_path = filedialog.askopenfilename(filetypes=[("Mini Language", "*.mc"), ("Text Files", "*.txt"), ("All Files", "*.*")])
        if file_path:
            with open(file_path, "r") as f:
                self.editor.delete("1.0", tk.END)
                self.editor.insert(tk.END, f.read())
            self.status_bar.config(text=f"Loaded: {os.path.basename(file_path)}", bg="#007acc")

    def run_compiler(self):
        # 1. Check if compiler executable exists
        compiler_bin = "./compiler.exe" if os.name == 'nt' else "./compiler"
        if not os.path.exists(compiler_bin):
            messagebox.showerror("Error", f"Executable '{compiler_bin}' not found!\nPlease compile your project first using gcc.")
            return

        # 2. Save editor text to a temporary test file
        temp_file = "temp_input.mc"
        code_content = self.editor.get("1.0", tk.END).strip()
        if not code_content:
            messagebox.showwarning("Warning", "Editor is empty! Please write some code first.")
            return

        with open(temp_file, "w") as f:
            f.write(code_content)

        # 3. Execute compiler.exe with temp_file
        self.status_bar.config(text="Compiling...", bg="#e2c08d", fg="black")
        self.root.update_idletasks()

        try:
            result = subprocess.run([compiler_bin, temp_file], capture_output=True, text=True)
            self.output.delete("1.0", tk.END)

            if result.stdout:
                self.output.insert(tk.END, result.stdout)
            if result.stderr:
                self.output.insert(tk.END, "\n--- ERRORS DETECTED ---\n", "error")
                self.output.insert(tk.END, result.stderr)

            if result.returncode == 0:
                self.status_bar.config(text="Compilation Successful! Three-Address Code (TAC) Generated.", bg="#16825d", fg="white")
            else:
                self.status_bar.config(text="Compilation Failed with Errors.", bg="#f44747", fg="white")

        except Exception as e:
            self.output.insert(tk.END, f"Execution failed: {str(e)}")
            self.status_bar.config(text="Execution error.", bg="#f44747", fg="white")

        # Cleanup temp file
        if os.path.exists(temp_file):
            os.remove(temp_file)

if __name__ == "__main__":
    root = tk.Tk()
    app = ModernCompilerGUI(root)
    root.mainloop()