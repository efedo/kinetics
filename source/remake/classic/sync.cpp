// Between the original program's globals and the shared session.

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "classic/kinetics.hpp"
#include "core/models.hpp"

namespace classic {
namespace {

Session* g_session = nullptr;

float finite_or_zero(double v) { return std::isfinite(v) ? static_cast<float>(v) : 0.0f; }

// The globals hold floats, as v3.51 did. Going back to the session, use the
// shortest decimal that reads back as the same float, so 1.6583f becomes
// 1.6583 rather than 1.65830004215240479.
double float_to_decimal(float f) {
    if (!std::isfinite(f)) return f;
    char buf[32];
    const auto res = std::to_chars(buf, buf + sizeof buf, f);
    return std::strtod(std::string(buf, res.ptr).c_str(), nullptr);
}

void copy_string(char* dest, size_t size, const std::string& s) { std::snprintf(dest, size, "%s", s.c_str()); }

}  // namespace

Session& session() { return *g_session; }
void set_session(Session* s) { g_session = s; }

kin::ModelId model_of(int eqnType) { return static_cast<kin::ModelId>(eqnType); }
int eqn_type_of(kin::ModelId id) { return static_cast<int>(id); }

// KIN_EDIT.CPP select_parameters, unchanged.
void select_parameters() {
    std::strcpy(p_name[1], "Km");
    for (int i = 2; i <= 4; i++) std::strcpy(p_name[i], " ");
    num_parameters = MAX_PARMS;
    for (int i = 1; i <= num_parameters; i++) {
        p_max[i] = 1e6f;
        p_min[i] = 1e-6f;
    }
    p_max[2] = 20.0f;
    p_min[2] = 0.01f;
    HILL = ACTIVATION = INHIBITION = NO;

    if (eqn_type == MM) {
        std::strcpy(p_name[2], "Vmax");
        num_parameters = 2;
        p_max[2] = 1e6f;
        p_min[2] = 1e-6f;
    } else if (eqn_type == HILL_G0 || eqn_type == HILL_G1) {
        HILL = YES;
        std::strcpy(p_name[2], "h");
        std::strcpy(p_name[3], "Vmax");
        num_parameters = 3;
        if (eqn_type == HILL_G1) p_min[2] = 1.0f;
    } else if (eqn_type == ACT_G0 || eqn_type == ACT_G1 || eqn_type == ACT_E1) {
        ACTIVATION = YES;
        std::strcpy(p_name[1], "Ka");
        std::strcpy(p_name[2], "h");
        std::strcpy(p_name[3], "Vmax");
        std::strcpy(p_name[4], "v0");
        if (eqn_type == ACT_G1) p_min[2] = 1.0f;
        if (eqn_type == ACT_E1) {
            num_parameters = 3;
            std::strcpy(p_name[2], "Vmax");
            std::strcpy(p_name[3], "v0");
            std::strcpy(p_name[4], " ");
            p_min[2] = 1e-6f;
            p_max[2] = 1e6f;
        }
    } else if (eqn_type == INH_L || eqn_type == INH_I50 || eqn_type == INH_K50) {
        INHIBITION = YES;
        if (eqn_type == INH_L) {
            std::strcpy(p_name[1], "I50");
            std::strcpy(p_name[2], "v0");
            num_parameters = 2;
            p_min[2] = 1e-6f;
            p_max[2] = 1e6f;
        } else {
            std::strcpy(p_name[2], "h");
            std::strcpy(p_name[3], "vmin");
            std::strcpy(p_name[4], "v0");
            if (eqn_type == INH_I50) std::strcpy(p_name[1], "I50");
            if (eqn_type == INH_K50) std::strcpy(p_name[1], "K50");
        }
    } else if (eqn_type == ARR) {
        std::strcpy(p_name[1], "log10(A)");
        std::strcpy(p_name[2], "Ea (Kcal/mol)");
        std::strcpy(p_name[3], "Ea (KJ/mol)");
        num_parameters = 2;
        p_min[1] = 1e-6f;
        p_max[1] = 100.0f;
        p_min[2] = p_min[3] = 1e-6f;
        p_max[2] = p_max[3] = 200.0f;
    } else if (eqn_type == LINEAR) {
        std::strcpy(p_name[1], "Intercept");
        std::strcpy(p_name[2], "Slope");
        num_parameters = 2;
        p_min[1] = p_min[2] = -1.0e6f;
        p_max[1] = p_max[2] = 1.0e6f;
    }
}

void load_from_session() {
    auto& s = session();

    // Equation, fixed flags and values. The session's bounds are the ones
    // the fit uses, so show those.
    eqn_type = eqn_type_of(s.model());
    EQN_CHOSEN = YES;
    select_parameters();
    Vmax = v0 = vmin = INTERCEPT = VARIABLE;
    const auto& def = kin::model_def(s.model());
    const auto& ps = s.params();
    for (size_t j = 0; j < def.params.size() && j < ps.size() && j + 1 <= MAX_PARMS; ++j) {
        const size_t i = j + 1;
        p_min[i] = static_cast<float>(ps[j].min);
        p_max[i] = static_cast<float>(ps[j].max);
        p[i] = static_cast<float>(ps[j].value);
        if (!ps[j].fixed) continue;
        switch (def.params[j].role) {
            case kin::Role::Vmax: Vmax = FIXED; break;
            case kin::Role::V0: v0 = FIXED; break;
            case kin::Role::Vmin: vmin = FIXED; break;
            case kin::Role::Intercept: INTERCEPT = FIXED; break;
            default: break;
        }
    }
    GUESS_TYPE = s.guess() == kin::GuessMode::Manual ? MANUAL : AUTOMATIC;
    MINIMIZATION_TYPE = s.method() == kin::Method::GaussNewton ? GAUSS_N : MARQUARDT;

    // Data.
    const auto pts = s.table();
    num_vals = 0;
    for (const auto& pt : pts) {
        if (num_vals >= MAX_VALUES) break;
        ++num_vals;
        val[1][num_vals] = static_cast<float>(pt.x);
        val[2][num_vals] = static_cast<float>(pt.y);
    }
    num_per_column[1] = num_per_column[2] = num_vals;
    DATA_ENTERED = num_vals > 0 ? YES : NO;
    if (num_vals > 0) {
        Y_min = Y_max = val[2][1];
        for (int i = 2; i <= num_vals; ++i) {
            if (val[2][i] > Y_max) Y_max = val[2][i];
            if (val[2][i] < Y_min) Y_min = val[2][i];
        }
    }

    // Results: shown only if they match the current equation and data.
    PARMS_CALCULATED = NO;
    if (const auto* a = s.result(); a && !s.result_stale() && a->model() == s.model()) {
        for (size_t j = 0; j < a->fit.p.size() && j + 1 <= MAX_PARMS; ++j) {
            p[j + 1] = static_cast<float>(a->fit.p[j]);
            sd[j + 1] = finite_or_zero(a->fit.sd[j]);
        }
        if (eqn_type == ARR) {
            p[3] = p[2] * 4.184f;
            sd[3] = sd[2] * 4.184f;
        }
        std_dev = finite_or_zero(a->fit.stdDev);
        N_ITERS = a->fit.iterations;
        if (a->linear) corr = static_cast<float>(a->linear->r);
        PARMS_CALCULATED = YES;
    }

    // Imported file.
    const auto& im = s.import_state();
    import.type = im.type == app::ImportType::Spreadsheet ? DBASE : (im.type == app::ImportType::Mpa ? MPA : NONE);
    copy_string(import.path, sizeof import.path, im.path.parent_path().string());
    copy_string(import.name, sizeof import.name, im.path.filename().string());

    // Output (report) options.
    const auto& rs = s.report_state();
    output.format = static_cast<int>(rs.options.format);
    copy_string(output.header, sizeof output.header, rs.options.header);
    copy_string(output.expt_date, sizeof output.expt_date, rs.options.experimentDate);
    output.numbering = static_cast<int>(rs.options.numbering);
    output.newfile = rs.newFile ? YES : NO;
    if (!rs.file.empty()) {
        if (output.location == NONE) output.location = TO_FILE;
        copy_string(output.path, sizeof output.path, rs.file.parent_path().string());
        copy_string(output.name, sizeof output.name, rs.file.filename().string());
    }
    FILENUM = rs.nextNumber;
}

void store_equation() {
    auto& s = session();
    const auto m = model_of(eqn_type);
    if (s.model() != m) s.set_model(m);
    auto& ps = s.params();
    const auto& def = kin::model_def(m);
    for (size_t j = 0; j < def.params.size() && j < ps.size(); ++j) {
        int flag = -1;
        switch (def.params[j].role) {
            case kin::Role::Vmax: flag = Vmax; break;
            case kin::Role::V0: flag = v0; break;
            case kin::Role::Vmin: flag = vmin; break;
            case kin::Role::Intercept: flag = INTERCEPT; break;
            default: break;
        }
        if (flag < 0) continue;
        ps[j].fixed = flag == FIXED;
        if (ps[j].fixed) ps[j].value = float_to_decimal(p[j + 1]);
    }
    s.changed();
}

void store_data(const std::string& source, std::optional<int> run) {
    std::vector<kin::Point> pts;
    for (int i = 1; i <= num_vals; ++i) pts.push_back({float_to_decimal(val[1][i]), float_to_decimal(val[2][i])});
    session().set_table(pts, source, run);
    DATA_ENTERED = num_vals > 0 ? YES : NO;
}

void store_results(const kin::Analysis& a, std::optional<int> run) {
    for (size_t j = 0; j < a.fit.p.size() && j + 1 <= MAX_PARMS; ++j) {
        p[j + 1] = static_cast<float>(a.fit.p[j]);
        sd[j + 1] = finite_or_zero(a.fit.sd[j]);
    }
    if (eqn_type == ARR) {
        p[3] = p[2] * 4.184f;
        sd[3] = sd[2] * 4.184f;
    }
    std_dev = finite_or_zero(a.fit.stdDev);
    N_ITERS = a.fit.iterations;
    if (a.linear) {
        corr = static_cast<float>(a.linear->r);
        DF[1] = a.linear->df;
    }
    session().set_result(a, run);
    PARMS_CALCULATED = YES;
}

void store_guess_and_minimization() {
    auto& s = session();
    s.guess() = GUESS_TYPE == MANUAL ? kin::GuessMode::Manual : kin::GuessMode::Automatic;
    s.method() = MINIMIZATION_TYPE == GAUSS_N ? kin::Method::GaussNewton : kin::Method::Marquardt;
    s.changed();
}

void store_output_options() {
    auto& rs = session().report_state();
    rs.options.format = static_cast<kin::ReportFormat>(output.format);
    rs.options.header = output.header;
    rs.options.experimentDate = output.expt_date;
    rs.options.numbering = static_cast<kin::Numbering>(output.numbering);
    // v3.51 wrote comma-separated SHORT/MEDIUM reports to files and text
    // columns to the printer.
    rs.options.layout = output.location == TO_FILE ? kin::ReportLayout::Csv : kin::ReportLayout::Text;
    session().save_settings();
}

}  // namespace classic
