// Markdown → terminal UTF-8 (optional ANSI) via md4c callbacks. GFM flags mirror md.cpp.

#include "html/md_terminal.h"
#include "html/html.h"

#include <md4c.h>

#include <algorithm>
#include <string>
#include <vector>

namespace html {
namespace {

std::string attr_to_utf8(const MD_ATTRIBUTE& a)
{
    if (!a.text || a.size == 0)
        return {};
    return std::string(a.text, a.size);
}

unsigned build_terminal_md_flags(const MdOptions& o)
{
    unsigned f = 0;
    if (o.tables)
        f |= MD_FLAG_TABLES;
    if (o.strikethrough)
        f |= MD_FLAG_STRIKETHROUGH;
    if (o.task_lists)
        f |= MD_FLAG_TASKLISTS;
    if (o.autolinks)
        f |= MD_FLAG_PERMISSIVEAUTOLINKS;
    if (o.permissive_url)
        f |= MD_FLAG_PERMISSIVEURLAUTOLINKS | MD_FLAG_PERMISSIVEEMAILAUTOLINKS;
    if (!o.fenced_code) {
        // keep indented code enabled when fenced-only not requested
    }
    if (o.no_html)
        f |= MD_FLAG_NOHTML;
    f |= MD_FLAG_COLLAPSEWHITESPACE;
    return f;
}

static void strip_ansi_inplace(std::string& t)
{
    std::string o;
    o.reserve(t.size());
    for (size_t i = 0; i < t.size();) {
        if (t[i] == '\x1b' && i + 1 < t.size() && t[i + 1] == '[') {
            i += 2;
            while (i < t.size() && t[i] != 'm')
                ++i;
            if (i < t.size())
                ++i;
            continue;
        }
        o.push_back(t[i++]);
    }
    t.swap(o);
}

static void normalize_cell_inplace(std::string& t)
{
    strip_ansi_inplace(t);
    std::string r;
    r.reserve(t.size());
    bool pending_space = false;
    for (unsigned char uc : t) {
        const char c = static_cast<char>(uc);
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            pending_space = true;
        } else {
            if (pending_space && !r.empty())
                r += ' ';
            pending_space = false;
            r += c;
        }
    }
    if (pending_space && !r.empty())
        r += ' ';
    t = std::move(r);
}

static std::string pad_cell_display(const std::string& cell, unsigned w, MD_ALIGN align)
{
    const auto n = static_cast<unsigned>(cell.size());
    if (n >= w)
        return cell.substr(0, w);
    const unsigned pad = w - n;
    if (align == MD_ALIGN_RIGHT)
        return std::string(pad, ' ') + cell;
    if (align == MD_ALIGN_CENTER) {
        const unsigned L = pad / 2;
        const unsigned R = pad - L;
        return std::string(L, ' ') + cell + std::string(R, ' ');
    }
    return cell + std::string(pad, ' ');
}

static std::string table_sep_dashes(unsigned w, MD_ALIGN a)
{
    unsigned mid = std::max(3u, w);
    std::string d(mid, '-');
    if (a == MD_ALIGN_LEFT)
        d[0] = ':';
    else if (a == MD_ALIGN_RIGHT)
        d[mid - 1] = ':';
    else if (a == MD_ALIGN_CENTER) {
        d[0]         = ':';
        d[mid - 1]   = ':';
    }
    return d;
}

struct TermRender {
    std::string              out;
    bool                     color = false;
    int                      width_cols = 0; // reserved for soft-wrap
    bool                     need_block_gap = false;
    std::vector<std::string> sgr_stack;
    /// When non-null, inline text and SGR go here (GFM table cells); otherwise @ref out.
    std::string*             active_buf = nullptr;

    std::string& act()
    {
        return active_buf ? *active_buf : out;
    }

    void sgr_push(const char* seq)
    {
        if (!color || !seq)
            return;
        act() += seq;
        sgr_stack.emplace_back(seq);
    }

    void sgr_pop_one()
    {
        if (!color || sgr_stack.empty())
            return;
        sgr_stack.pop_back();
        act() += "\033[0m";
        for (const auto& s : sgr_stack)
            act() += s;
    }

    void sgr_reset_all()
    {
        if (!color)
            return;
        sgr_stack.clear();
        act() += "\033[0m";
    }

    /** Drop active SGR stack without writing reset bytes (used between table cells). */
    void sgr_stack_clear_only()
    {
        sgr_stack.clear();
    }

    void append_raw(const MD_CHAR* t, MD_SIZE sz)
    {
        if (t && sz)
            act().append(t, sz);
    }

    // ── GFM table accumulation (md4c: THEAD/TBODY → TR → TH/TD) ─────────────
    std::vector<std::vector<std::pair<std::string, MD_ALIGN>>> table_rows;
    std::vector<std::pair<std::string, MD_ALIGN>>             current_row;
    std::string                                               current_cell;
    MD_ALIGN                                                  pending_cell_align = MD_ALIGN_DEFAULT;

    static std::pair<std::string, MD_ALIGN> cell_at(const std::vector<std::pair<std::string, MD_ALIGN>>& row,
                                                    unsigned i)
    {
        if (i < row.size())
            return row[i];
        return {{}, MD_ALIGN_DEFAULT};
    }

    void flush_table()
    {
        if (table_rows.empty())
            return;
        unsigned max_cols = 0;
        for (const auto& r : table_rows)
            max_cols = std::max(max_cols, static_cast<unsigned>(r.size()));
        if (max_cols == 0)
            return;

        std::vector<unsigned> w(max_cols, 3u);
        for (const auto& r : table_rows) {
            for (unsigned i = 0; i < r.size(); ++i)
                w[i] = std::max(w[i], static_cast<unsigned>(r[i].first.size()));
        }

        const auto* header_row = &table_rows[0];

        auto emit_row_line = [&](const std::vector<std::pair<std::string, MD_ALIGN>>& row) {
            out += '|';
            for (unsigned i = 0; i < max_cols; ++i) {
                const auto [txt, al] = cell_at(row, i);
                out += ' ';
                out += pad_cell_display(txt, w[i], al);
                out += " |";
            }
            out += '\n';
        };

        auto emit_sep_line = [&]() {
            out += '|';
            for (unsigned i = 0; i < max_cols; ++i) {
                MD_ALIGN a = MD_ALIGN_DEFAULT;
                if (i < header_row->size())
                    a = (*header_row)[i].second;
                out += ' ';
                out += table_sep_dashes(w[i], a);
                out += " |";
            }
            out += '\n';
        };

        emit_row_line(table_rows[0]);
        emit_sep_line();
        for (size_t ri = 1; ri < table_rows.size(); ++ri)
            emit_row_line(table_rows[ri]);

        table_rows.clear();
    }

    void ensure_gap_before_block()
    {
        (void)width_cols;
        if (need_block_gap) {
            if (!out.empty() && out.back() != '\n')
                out += '\n';
            out += '\n';
        }
        need_block_gap = true;
    }

    int enter_block(MD_BLOCKTYPE type, void* detail)
    {
        switch (type) {
        case MD_BLOCK_DOC:
            return 0;
        case MD_BLOCK_QUOTE:
            ensure_gap_before_block();
            sgr_push("\033[2m\033[35m");
            out += "> ";
            sgr_reset_all();
            break;
        case MD_BLOCK_UL:
            ensure_gap_before_block();
            break;
        case MD_BLOCK_OL:
            ensure_gap_before_block();
            break;
        case MD_BLOCK_LI: {
            auto* d = static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
            out += "  ";
            if (d && d->is_task) {
                out += (d->task_mark == 'x' || d->task_mark == 'X') ? "[x] " : "[ ] ";
            } else
                out += "* ";
            break;
        }
        case MD_BLOCK_HR:
            ensure_gap_before_block();
            sgr_push("\033[2m");
            out += "────────────────────────────────────────\n";
            sgr_reset_all();
            break;
        case MD_BLOCK_H: {
            ensure_gap_before_block();
            auto* d = static_cast<const MD_BLOCK_H_DETAIL*>(detail);
            const unsigned lvl = d && d->level >= 1 && d->level <= 6 ? d->level : 1u;
            if (color) {
                if (lvl <= 2)
                    sgr_push("\033[1m\033[33m");
                else if (lvl == 3)
                    sgr_push("\033[1m\033[36m");
                else
                    sgr_push("\033[1m\033[37m");
            }
            for (unsigned i = 0; i < lvl; ++i)
                out += '#';
            out += ' ';
            break;
        }
        case MD_BLOCK_CODE: {
            ensure_gap_before_block();
            auto* d = static_cast<const MD_BLOCK_CODE_DETAIL*>(detail);
            if (color)
                sgr_push("\033[2m\033[90m");
            out += "```";
            if (d && d->lang.size > 0) {
                append_raw(d->lang.text, d->lang.size);
            }
            out += '\n';
            break;
        }
        case MD_BLOCK_HTML:
            ensure_gap_before_block();
            break;
        case MD_BLOCK_P:
            ensure_gap_before_block();
            break;
        case MD_BLOCK_TABLE:
            ensure_gap_before_block();
            table_rows.clear();
            current_row.clear();
            current_cell.clear();
            active_buf = nullptr;
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;
        case MD_BLOCK_TR:
            current_row.clear();
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            auto* d = static_cast<const MD_BLOCK_TD_DETAIL*>(detail);
            pending_cell_align = d ? d->align : MD_ALIGN_DEFAULT;
            active_buf        = &current_cell;
            current_cell.clear();
            break;
        }
        default:
            break;
        }
        return 0;
    }

    int leave_block(MD_BLOCKTYPE type, void* /*detail*/)
    {
        switch (type) {
        case MD_BLOCK_H:
            out += '\n';
            sgr_reset_all();
            break;
        case MD_BLOCK_CODE:
            sgr_reset_all();
            out += "```\n";
            break;
        case MD_BLOCK_P:
            out += '\n';
            break;
        case MD_BLOCK_QUOTE:
            out += '\n';
            break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            out += '\n';
            break;
        case MD_BLOCK_LI:
            out += '\n';
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            sgr_stack_clear_only();
            normalize_cell_inplace(current_cell);
            current_row.emplace_back(std::move(current_cell), pending_cell_align);
            pending_cell_align = MD_ALIGN_DEFAULT;
            active_buf         = nullptr;
            current_cell.clear();
            break;
        case MD_BLOCK_TR:
            table_rows.push_back(std::move(current_row));
            current_row.clear();
            break;
        case MD_BLOCK_TABLE:
            flush_table();
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;
        default:
            break;
        }
        return 0;
    }

    int enter_span(MD_SPANTYPE type, void* detail)
    {
        switch (type) {
        case MD_SPAN_EM:
            sgr_push(color ? "\033[3m" : "");
            break;
        case MD_SPAN_STRONG:
            sgr_push(color ? "\033[1m" : "");
            break;
        case MD_SPAN_DEL:
            sgr_push(color ? "\033[9m" : "");
            break;
        case MD_SPAN_CODE:
            sgr_push(color ? "\033[36m" : "");
            break;
        case MD_SPAN_A:
            break;
        case MD_SPAN_IMG:
            sgr_push(color ? "\033[2m" : "");
            act() += "[image: ";
            break;
        default:
            break;
        }
        (void)detail;
        return 0;
    }

    int leave_span(MD_SPANTYPE type, void* detail)
    {
        switch (type) {
        case MD_SPAN_EM:
        case MD_SPAN_STRONG:
        case MD_SPAN_DEL:
        case MD_SPAN_CODE:
            sgr_pop_one();
            break;
        case MD_SPAN_A: {
            auto* d = static_cast<const MD_SPAN_A_DETAIL*>(detail);
            if (d) {
                const std::string href = attr_to_utf8(d->href);
                if (!href.empty()) {
                    if (color) {
                        act() += "\033[2m";
                    }
                    act() += " (";
                    act() += href;
                    act() += ')';
                    if (color) {
                        act() += "\033[0m";
                        for (const auto& s : sgr_stack)
                            act() += s;
                    }
                }
            }
            break;
        }
        case MD_SPAN_IMG:
            act() += ']';
            sgr_pop_one();
            break;
        default:
            break;
        }
        return 0;
    }

    int on_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size)
    {
        switch (type) {
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE:
        case MD_TEXT_HTML:
            append_raw(text, size);
            break;
        case MD_TEXT_NULLCHAR:
            act() += "\xEF\xBF\xBD";
            break;
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR:
            act() += '\n';
            break;
        case MD_TEXT_ENTITY:
            append_raw(text, size);
            break;
        case MD_TEXT_LATEXMATH:
            append_raw(text, size);
            break;
        default:
            break;
        }
        return 0;
    }
};

static int cb_enter_block(MD_BLOCKTYPE t, void* d, void* ud)
{
    return static_cast<TermRender*>(ud)->enter_block(t, d);
}
static int cb_leave_block(MD_BLOCKTYPE t, void* d, void* ud)
{
    return static_cast<TermRender*>(ud)->leave_block(t, d);
}
static int cb_enter_span(MD_SPANTYPE t, void* d, void* ud)
{
    return static_cast<TermRender*>(ud)->enter_span(t, d);
}
static int cb_leave_span(MD_SPANTYPE t, void* d, void* ud)
{
    return static_cast<TermRender*>(ud)->leave_span(t, d);
}
static int cb_text(MD_TEXTTYPE ty, const MD_CHAR* text, MD_SIZE size, void* ud)
{
    return static_cast<TermRender*>(ud)->on_text(ty, text, size);
}

} // namespace

std::string markdown_to_terminal(const std::string& markdown, const MdOptions& md_opts, bool ansi_color,
                                 int terminal_width_cols)
{
    TermRender ctx;
    ctx.color        = ansi_color;
    ctx.width_cols   = terminal_width_cols;

    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags       = build_terminal_md_flags(md_opts);
    parser.enter_block   = cb_enter_block;
    parser.leave_block   = cb_leave_block;
    parser.enter_span    = cb_enter_span;
    parser.leave_span    = cb_leave_span;
    parser.text          = cb_text;
    parser.debug_log     = nullptr;
    parser.syntax        = nullptr;

    const int rc =
        md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &ctx);
    if (rc != 0) {
        // Aborted or error — return original
        return markdown;
    }
    ctx.sgr_reset_all();
    // Trim excessive trailing newlines
    while (ctx.out.size() > 1 && ctx.out.back() == '\n' && ctx.out[ctx.out.size() - 2] == '\n')
        ctx.out.pop_back();
    if (!ctx.out.empty() && ctx.out.back() != '\n')
        ctx.out += '\n';
    return ctx.out.empty() ? markdown : ctx.out;
}

} // namespace html
