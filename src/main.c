#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/xon_api.h"
#include "logger.h"

static int ends_with(const char* str, const char* suffix) {
    size_t str_len;
    size_t suffix_len;
    if (!str || !suffix) return 0;
    str_len = strlen(str);
    suffix_len = strlen(suffix);
    if (suffix_len > str_len) return 0;
    return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

static int write_text_file(const char* path, const char* content) {
    FILE* out = fopen(path, "w");
    if (!out) {
        perror("Failed to open output file");
        return 1;
    }
    if (fputs(content, out) == EOF) {
        perror("Failed to write output");
        fclose(out);
        return 1;
    }
    fclose(out);
    return 0;
}

static void print_usage(const char* program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s <file.xon>\n"
            "  %s parse <file.xon>\n"
            "  %s validate <file.xon>\n"
            "  %s format <input.xon> [-o output.xon]\n"
            "  %s convert <input.(xon|json)> <output.(json|xon)>\n"
            "  %s eval <file.xon>\n",
            program, program, program, program, program, program);
    xon_log_warn("cli", "Invalid CLI usage invoked");
}

static int cmd_parse(const char* input_path) {
    XonValue* root = xonify(input_path);
    if (!root) {
        fprintf(stderr, "Parse failed for %s\n", input_path);
        xon_log_error("cli", "Parse failed for %s", input_path);
        return 1;
    }

    printf("Parse successful: %s\n", input_path);
    xon_log_info("cli", "Parse successful for %s", input_path);
    xon_print(root);
    xon_free(root);
    return 0;
}

static int cmd_validate(const char* input_path) {
    XonValue* root = xonify(input_path);
    if (!root) {
        fprintf(stderr, "Invalid Xon: %s\n", input_path);
        xon_log_error("cli", "Validation failed for %s", input_path);
        return 1;
    }
    printf("Valid Xon: %s\n", input_path);
    xon_log_info("cli", "Validation succeeded for %s", input_path);
    xon_free(root);
    return 0;
}

static int cmd_format(const char* input_path, const char* output_path) {
    XonValue* root = xonify(input_path);
    char* formatted;
    int rc = 0;

    if (!root) {
        fprintf(stderr, "Parse failed for %s\n", input_path);
        xon_log_error("cli", "Format parse failed for %s", input_path);
        return 1;
    }

    formatted = xon_to_xon(root, 1);
    if (!formatted) {
        fprintf(stderr, "Failed to format %s\n", input_path);
        xon_log_error("cli", "Formatting failed for %s", input_path);
        xon_free(root);
        return 1;
    }

    if (output_path) {
        rc = write_text_file(output_path, formatted);
        if (!rc) {
            printf("Formatted Xon written to %s\n", output_path);
            xon_log_info("cli", "Formatted output written to %s", output_path);
        }
    } else {
        printf("%s\n", formatted);
        xon_log_info("cli", "Formatted output written to stdout");
    }

    xon_string_free(formatted);
    xon_free(root);
    return rc;
}

static int cmd_convert(const char* input_path, const char* output_path) {
    XonValue* root = xonify(input_path);
    char* serialized = NULL;
    int rc;

    if (!root) {
        fprintf(stderr, "Parse failed for %s\n", input_path);
        xon_log_error("cli", "Convert parse failed for %s", input_path);
        return 1;
    }

    if (ends_with(output_path, ".json")) {
        serialized = xon_to_json(root, 1);
    } else if (ends_with(output_path, ".xon")) {
        serialized = xon_to_xon(root, 1);
    } else {
        fprintf(stderr, "Unsupported output extension: %s\n", output_path);
        xon_log_warn("cli", "Unsupported output extension: %s", output_path);
        xon_free(root);
        return 1;
    }

    if (!serialized) {
        fprintf(stderr, "Failed to convert %s\n", input_path);
        xon_log_error("cli", "Conversion failed for %s", input_path);
        xon_free(root);
        return 1;
    }

    rc = write_text_file(output_path, serialized);
    if (!rc) {
        printf("Converted %s -> %s\n", input_path, output_path);
        xon_log_info("cli", "Converted %s -> %s", input_path, output_path);
    }

    xon_string_free(serialized);
    xon_free(root);
    return rc;
}

static int cmd_eval(const char* input_path) {
    XonValue* root = xonify(input_path);
    XonValue* evaluated;
    char* rendered = NULL;
    int rc = 0;

    if (!root) {
        fprintf(stderr, "Parse failed for %s\n", input_path);
        xon_log_error("cli", "Evaluation parse failed for %s", input_path);
        return 1;
    }

    evaluated = xon_eval(root);
    if (!evaluated) {
        fprintf(stderr, "Evaluation failed for %s\n", input_path);
        xon_free(root);
        xon_log_error("cli", "Evaluation failed for %s", input_path);
        return 1;
    }

    rendered = xon_to_xon(evaluated, 1);
    if (!rendered) {
        fprintf(stderr, "Failed to serialize evaluation result for %s\n", input_path);
        xon_log_error("cli", "Evaluation serialization failed for %s", input_path);
        rc = 1;
    } else {
        printf("%s\n", rendered);
        xon_string_free(rendered);
        xon_log_info("cli", "Evaluation output written to stdout");
    }

    xon_free(evaluated);
    xon_free(root);
    return rc;
}

int main(int argc, char** argv) {
    const char* command;
    int rc = 1;

    xon_logger_init("xon-cli");
    xon_logger_set_directory("logs");
    xon_log_info("cli", "CLI invocation started");

    if (argc == 2) {
        rc = cmd_parse(argv[1]);
        xon_shutdown_logging();
        return rc;
    }

    if (argc < 3) {
        print_usage(argv[0]);
        xon_shutdown_logging();
        return 1;
    }

    command = argv[1];

    if (strcmp(command, "parse") == 0) {
        rc = cmd_parse(argv[2]);
        xon_shutdown_logging();
        return rc;
    }

    if (strcmp(command, "validate") == 0) {
        rc = cmd_validate(argv[2]);
        xon_shutdown_logging();
        return rc;
    }

    if (strcmp(command, "format") == 0) {
        if (argc == 3) {
            rc = cmd_format(argv[2], NULL);
            xon_shutdown_logging();
            return rc;
        }
        if (argc == 5 && strcmp(argv[3], "-o") == 0) {
            rc = cmd_format(argv[2], argv[4]);
            xon_shutdown_logging();
            return rc;
        }
        print_usage(argv[0]);
        xon_shutdown_logging();
        return 1;
    }

    if (strcmp(command, "convert") == 0) {
        if (argc != 4) {
            print_usage(argv[0]);
            xon_shutdown_logging();
            return 1;
        }
        rc = cmd_convert(argv[2], argv[3]);
        xon_shutdown_logging();
        return rc;
    }

    if (strcmp(command, "eval") == 0) {
        if (argc != 3) {
            print_usage(argv[0]);
            xon_shutdown_logging();
            return 1;
        }
        rc = cmd_eval(argv[2]);
        xon_shutdown_logging();
        return rc;
    }

    print_usage(argv[0]);
    xon_shutdown_logging();
    return 1;
}
