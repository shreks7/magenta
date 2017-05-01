// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <ctime>

#include "generator.h"

using std::string;

constexpr char kAuthors[] = "The Fuchsia Authors";

bool generate_file_header(std::ofstream& os, const string& type) {
    auto t = std::time(nullptr);
    auto ltime = std::localtime(&t);

    os << "// Copyright " << ltime->tm_year + 1900
       << " " << kAuthors << ". All rights reserved.\n";
    os << "// This is a GENERATED file. The license governing this file can be ";
    os << "found in the LICENSE file.\n\n";

    if (type == "rust") {
        os << "#[link(name = \"magenta\")]\n";
        os << "extern {\n";
    }

    return os.good();
}

bool generate_file_trailer(std::ofstream& os, const string& type) {
    os << "\n";

    if (type == "rust") {
        os << "}\n";
    }

    return os.good();
}

const string add_attribute(std::map<string, string> attributes,
    const string& attribute) {
    auto ft = attributes.find(attribute);
    return (ft == attributes.end()) ? string() : ft->second;
}

bool generate_legacy_header(std::ofstream& os, const Syscall& sc,
    const string& function_prefix, const std::vector<string>& name_prefixes,
    const string& no_args_type, bool allow_pointer_wrapping,
    const std::map<string, string>& attributes) {
    constexpr uint32_t indent_spaces = 4u;

    for (auto name_prefix : name_prefixes) {
        auto syscall_name = name_prefix + sc.name;

        os << function_prefix;

        // writes "[return-type] prefix_[syscall-name]("
        os << sc.return_type() << " " << syscall_name << "(";

       // Writes all arguments.
        sc.for_each_kernel_arg([&](const TypeSpec& arg) {
            os << "\n" << string(indent_spaces, ' ')
               << arg.as_cpp_declaration(
                        allow_pointer_wrapping && !sc.is_no_wrap() && !sc.is_vdso()) << ",";
        });

        if (!os.good()) {
            return false;
        }

        if (sc.num_kernel_args() > 0) {
            // remove the comma.
            os.seekp(-1, std::ios_base::end);
        } else {
            os << no_args_type;
        }

        os << ") ";

        // Writes attributes after arguments.
        for (const auto& attr : sc.attributes) {
            auto a = add_attribute(attributes, attr);
            if (!a.empty())
                os << a << " ";
        }

        os.seekp(-1, std::ios_base::end);

        os << ";\n\n";

        syscall_name = "_" + syscall_name;
    }

    return os.good();
}

bool generate_rust_bindings(std::ofstream& os, const Syscall& sc) {
    os << "    pub fn mx_" << sc.name << "(";

    // Writes all arguments.
    sc.for_each_kernel_arg([&](const TypeSpec& arg) {
        os << "\n        "
            << arg.as_rust_declaration() << ",";
    });

    if (!os.good()) {
        return false;
    }

    if (sc.num_kernel_args() > 0) {
        // remove the comma.
        os.seekp(-1, std::ios_base::end);
    }
    // Finish off list and write return type
    os << "\n        )";
    if (sc.return_type() != "void") {
      os << " -> " << map_override(sc.return_type(), rust_primitives);
    }
    os << ";\n\n";

    return os.good();
}

bool generate_kernel_header(std::ofstream& os, const Syscall& sc,
    const string& name_prefix, const std::map<string, string>& attributes) {
    return sc.is_vdso()
        ? true
        : generate_legacy_header(os, sc, "", {name_prefix}, "", true, attributes);
}

string invocation(std::ofstream& os, const string& out_var, const string& out_type,
                  const string& syscall_name, const Syscall& sc) {
    if (sc.is_noreturn()) {
        // no return - no need to set anything. the compiler
        // should know that we're never going anywhere from here
        os << syscall_name << "(";
        return ")";
    }

    os << out_var << " = ";

    if (sc.is_void_return()) {
        // void function - synthesise an empty return value.
        // case 0: ret = 0; sys_andy(
        os << "0; " << syscall_name << "(";
        return ")";
    }
    // case 0: ret = static_cast<int64_t(sys_andy(
    os << "static_cast<" << out_type << ">(" << syscall_name << "(";
    return "))";
}

bool generate_kernel_code(std::ofstream& os, const Syscall& sc,
    const string& syscall_prefix, const string& return_var, const string& return_type,
    const string& arg_prefix) {

    if (sc.is_vdso())
        return true;

    string code_sp = string(8u, ' ');
    string block_sp = string(4u, ' ');
    string arg_sp = string(16u, ' ');

    auto syscall_name = syscall_prefix + sc.name;

    // case 0:
    os << "    case " << sc.index << ": {\n" << code_sp;

    // If blocking, open a "while(true)" so we can retry on thread suspend
    // TODO(teisenbe): Move this to be autogenerated VDSO code instead
    if (sc.is_blocking()) {
        os << "while (true) {\n";
    }

    // ret = static_cast<uint64_t>(syscall_whatevs(      )) -closer
    string close_invocation = invocation(os, return_var, return_type, syscall_name, sc);

    // Writes all arguments.
    int arg_index = 1;
    sc.for_each_kernel_arg([&](const TypeSpec& arg) {
        os << "\n" << arg_sp
           << sc.maybe_wrap(arg.as_cpp_cast(arg_prefix + std::to_string(arg_index++)))
           << ",";
    });

    if (!os.good()) {
        return false;
    }

    if (sc.num_kernel_args() > 0) {
        // remove the comma.
        os.seekp(-1, std::ios_base::end);
    }

    os << close_invocation;

    if (sc.is_noreturn()) {
        os << "; // __noreturn__\n" << block_sp << "}\n";
    } else {
        os << ";\n";
        // TODO(teisenbe): Move this to be autogenerated VDSO code instead
        if (sc.is_blocking()) {
            os << code_sp << block_sp << "if (likely(static_cast<mx_status_t>(" << return_var <<
               ") != ERR_INTERRUPTED_RETRY)) break;\n";
            os << code_sp << block_sp << "thread_process_pending_signals();\n";
            os << code_sp << "}\n";
        }
        os << code_sp << "break;\n" << block_sp << "}\n";
    }

    return os.good();
}

bool generate_legacy_assembly_x64(
    std::ofstream& os, const Syscall& sc, const string& syscall_macro, const string& name_prefix) {
    if (sc.is_vdso())
        return true;
    // SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) m_syscall nargs64, mx_##name, n
    os << syscall_macro << " " << sc.num_kernel_args() << " "
       << name_prefix << sc.name << " " << sc.index << "\n";
    return os.good();
}

bool generate_legacy_assembly_arm64(
    std::ofstream& os, const Syscall& sc, const string& syscall_macro, const string& name_prefix) {
    if (sc.is_vdso())
        return true;
    // SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) m_syscall mx_##name, n
    os << syscall_macro << " " << name_prefix << sc.name << " " << sc.index << "\n";
    return os.good();
}

bool generate_syscall_numbers_header(
    std::ofstream& os, const Syscall& sc, const string& define_prefix) {
    if (sc.is_vdso())
        return true;
    os << define_prefix << sc.name << " " << sc.index << "\n";
    return os.good();
}

bool generate_trace_info(std::ofstream& os, const Syscall& sc) {
    if (sc.is_vdso())
        return true;
    // Can be injected as an array of structs or into a tuple-like C++ container.
    os << "{" << sc.index << ", " << sc.num_kernel_args() << ", "
       << '"' << sc.name << "\"},\n";

    return os.good();
}

const std::map<string, string> user_attrs = {
    {"noreturn", "__attribute__((__noreturn__))"},
    {"const", "__attribute__((const))"},
    {"deprecated", "__attribute__((deprecated))"},

    // All vDSO calls are "leaf" in the sense of the GCC attribute.
    // It just means they can't ever call back into their callers'
    // own translation unit.  No vDSO calls make callbacks at all.
    {"*", "__attribute__((__leaf__))"},
};

const std::map<string, string> kernel_attrs = {
    {"noreturn", "__attribute__((__noreturn__))"},
};

#define gen1(name, arg1) std::bind(name, std::placeholders::_1, std::placeholders::_2, arg1)
#define gen2(name, arg1, arg2) std::bind(name, std::placeholders::_1, std::placeholders::_2, arg1, arg2)
#define gen4(name, arg1, arg2, arg3, arg4) std::bind(name, std::placeholders::_1, std::placeholders::_2, arg1, arg2, arg3, arg4)
#define gen5(name, arg1, arg2, arg3, arg4, arg5) std::bind(name, std::placeholders::_1, std::placeholders::_2, arg1, arg2, arg3, arg4, arg5)

const std::map<string, string> type_to_default_suffix = {
  {"user-header",   ".user.h"} ,
  {"vdso-header",   ".vdso.h"},
  {"kernel-header", ".kernel.h"},
  {"kernel-code",   ".kernel.inc"},
  {"x86-asm",   ".x86-64.S"},
  {"arm-asm",   ".arm64.S"},
  {"numbers",   ".syscall-numbers.h"},
  {"trace",   ".trace.inc"},
  {"rust",    ".rs"},
};

const std::map<string, gen> type_to_generator = {
    {
    // The user header, pure C.
        "user-header",
        gen5(generate_legacy_header,
            "extern ",                              // function prefix
            std::vector<string>({"mx_", "_mx_"}),   // function name prefixes
            "void",                                 // no-args special type
            false,
            user_attrs)
    },
    // The vDSO-internal header, pure C.  (VDsoHeaderC)
    {
        "vdso-header",
        gen5(generate_legacy_header,
            "__attribute__((visibility(\"hidden\"))) extern ",  // function prefix
            std::vector<string>({"VDSO_mx_"}),                  // function name prefixes
            "void",                                             // no args special type
            false,
            user_attrs)
    },
    // The kernel header, C++.
    {
        "kernel-header",
        gen2(generate_kernel_header,
            "sys_",                     // function prefix
            kernel_attrs)
    },
    // The kernel C++ code. A switch statement set.
    {
        "kernel-code",
        gen4(generate_kernel_code,
            "sys_",                     // function prefix
            "ret",                      // variable to assign invocation result to
            "uint64_t",                 // type of result variable
            "arg")                      // prefix for syscall arguments
    },
    //  The assembly file for x86-64.
    {
        "x86-asm",
        gen2(generate_legacy_assembly_x64,
            "m_syscall",                // syscall macro name
            "mx_")                      // syscall name prefix
    },
    //  The assembly include file for ARM64.
    {
        "arm-asm",
        gen2(generate_legacy_assembly_arm64,
            "m_syscall",                // syscall macro name
            "mx_")                      // syscall name prefix
    },
    // A C header defining MX_SYS_* syscall number macros.
    {
        "numbers",
        gen1(generate_syscall_numbers_header,
            "#define MX_SYS_")          // prefix for each syscall row
    },
    // The trace subsystem data, to be interpreted as an array of structs.
    {
        "trace",
        gen(generate_trace_info)
    },
    // The Rust bindings.
    {
        "rust",
        gen(generate_rust_bindings)
    },
};

const std::map<string, string>& get_type_to_default_suffix() {
  return type_to_default_suffix;
}

const std::map<string, gen>& get_type_to_generator() {
  return type_to_generator;
}

bool SysgenGenerator::AddSyscall(Syscall& syscall) {
    if (!syscall.validate())
        return false;
    syscall.assign_index(&next_index_);
    calls_.push_back(syscall);
    return true;
}

bool SysgenGenerator::Generate(const std::map<string, string>& type_to_filename) {
    for (auto& entry : type_to_filename) {
        if (!generate_one(entry.second, type_to_generator.at(entry.first), entry.first))
            return false;
    }
    return true;
}

bool SysgenGenerator::verbose() const { return verbose_; }

bool SysgenGenerator::generate_one(const string& output_file, const gen& generator, const string& type) {
    std::ofstream ofile;
    ofile.open(output_file.c_str(), std::ofstream::out);

    if (!generate_file_header(ofile, type)) {
        print_error("i/o error", output_file);
        return false;
    }

    if (!std::all_of(calls_.begin(), calls_.end(),
                    [&generator, &ofile](const Syscall& sc) {
                        return generator(ofile, sc);
                    })) {
        print_error("generation failed", output_file);
        return false;
    }

    if (!generate_file_trailer(ofile, type)) {
        print_error("i/o error", output_file);
        return false;
    }

    return true;
}

void SysgenGenerator::print_error(const char* what, const string& file) {
    fprintf(stderr, "error: %s for %s\n", what, file.c_str());
}