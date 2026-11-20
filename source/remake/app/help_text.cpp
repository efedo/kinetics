// Help topics, adapted from KIN_HLP.CPP of v3.51 for the new interface.

#include "app/help_text.hpp"

namespace app {
namespace {

constexpr HelpTopic kTopics[] = {
    {"Choosing an equation",
     "Select an equation from the Equation list (or Edit > Equation, Alt+T).\n\n"
     "The Michaelis-Menten and Hill equations describe single-substrate reactions. The Hill equation "
     "allows for cooperativity between enzyme subunits, which gives velocity vs. [S] curves that are "
     "sigmoidal rather than rectangular hyperbolas. Two Hill equations are offered: h > 0 and h >= 1. "
     "The h > 0 version can fit enzymes that show negative cooperativity; the h >= 1 version sets a "
     "lower limit of 1.0 on the Hill coefficient. Use it only when you are certain that your enzyme "
     "shows positive cooperativity. It is normal to fit with h > 0 first and look at the test of the "
     "Hill coefficient against 1.0, which is included in the results.\n\n"
     "Three ACTIVATOR equations are available. They are equivalent to the Michaelis-Menten and Hill "
     "equations except that the velocity is not zero without activator. Begin with the h > 0 version "
     "and switch to h >= 1 or h = 1 if the data allow. The h = 1 version is a Michaelis-Menten "
     "mechanism with a finite velocity at [A] = 0.\n\n"
     "Three INHIBITION equations are available. The linear inhibition equation is\n\n"
     "        v = v0 * I50 / (I50 + [I])\n\n"
     "where I50 is the concentration at which v = v0/2. It is the simplest inhibition equation and "
     "should be tried first. The non-linear I50 equation also returns I50 but fits curves that do not "
     "follow the linear equation. Both non-linear equations (I50 and K50) are empirical equations "
     "based on the Hill equation, so they also return a Hill coefficient as a measure of inhibitor "
     "cooperativity (it has no physical meaning). K50 is the mid-point of the curve, where "
     "v = (v0 - vmin)/2 + vmin.\n\n"
     "Two other equations are included for convenience: LINEAR regression and the ARRHENIUS equation\n\n"
     "        log10(v) = log10(A) - Ea / (2.303 R (T + 273.15))\n\n"
     "The Arrhenius equation needs base-10 logarithms of the velocities and temperatures in degrees "
     "Celsius; follow the column headings of the data table. Ea is reported in kcal/mol and kJ/mol.\n\n"
     "Parameters can be held fixed with the Fixed check box under Fitting options. For example, if you have several "
     "measurements at saturating substrate and so know Vmax, you can fix Vmax at that value. This is "
     "a convenience only: your data should span a wide enough range for the program to find all the "
     "parameters, and a fixed value must be accurate. When v0 is fixed and the data include a point "
     "at [A] = 0 or [I] = 0, the velocity measured there is used as v0."},

    {"Adding and editing data",
     "Concentrations ([S], [A] or [I]), X values, or temperatures (T in degrees C) go in the left "
     "column of the data table; velocities (v), Y values or log10(v) go in the right column.\n\n"
     "Click a cell and type; Tab moves to the next cell. Typing in the empty last row adds a new row. "
     "Right-click a row number to insert or delete rows. A cell that is not a number is shown in "
     "red and that row is left out of the fit; a row with a blank cell is also left out.\n\n"
     "You can paste two columns copied from a spreadsheet (Edit > Paste data from clipboard, or the Paste button), open a saved table "
     "(File > Open data table), and save the table as a comma-separated file.\n\n"
     "The program orders your data from lowest to highest X value before fitting. This simplifies "
     "some of the calculations and is done for convenience only; Edit > Sort data sorts the table "
     "itself."},

    {"Using file data",
     "You can analyse data saved as a text file in either of two formats. Choose File > Import "
     "spreadsheet file of runs or File > Import plate-reader file (MPA). The runs in the file are listed "
     "on the File runs tab, where you can load a run into the data table, fit a single run, or analyse "
     "every run. Close the tab, or choose File > Close imported file, when you have finished with the "
     "file.\n\n"
     "A spreadsheet file can be produced by Lotus 1-2-3, Quattro Pro or any spreadsheet by printing "
     "the data to a file, or by saving it as text. The format is:\n\n"
     "    S=NS\n"
     "    #     S1      S2      S3      S4  ...\n"
     "    RN1   v11     v12     v13     v14 ...\n"
     "    RN2   v21     v22     v23     v24 ...\n"
     "    #\n\n"
     "NS is the number of substrate columns (for seven columns the first line reads S=7). The '#' "
     "marks the top left corner of the table and is followed by the substrate concentrations, "
     "separated by spaces, commas or tabs. Each following row starts with the run number (RN) and "
     "lists one velocity per substrate column. Run 1 gives the pairs [S1, v11], [S2, v12], ... "
     "Set a velocity to -1 to leave that point out of one run without affecting the others. A "
     "final '#' on its own line marks the end of the table.\n\n"
     "In version 3.51 any negative value meant 'missing'. That is still the default, but you can "
     "turn it off on the File runs tab so that negative log10(v) or Y values can be analysed; only "
     "exactly -1 is then treated as missing.\n\n"
     "An MPA file is produced by the MicroPlate Analysis program (also from S. Brooks), which prints "
     "a table of cell number against slope (the velocity of each well). After three header lines "
     "each line holds one 'cell, velocity' record. Tell the program how many velocities make up "
     "one dataset; dataset 1 is the first block of that many records, dataset 2 the next, and so "
     "on. MPA files do not contain concentrations, so enter the [S] values in the data table first; "
     "importing a dataset fills the velocity column.\n\n"
     "Analyse all runs fits every run from the chosen run number to the end of the file. The results "
     "are listed in a table; click a run to see its results, graph and residuals, or step through "
     "them with the arrow buttons (this replaces the 'automatic preview' of version 3.51). Tick "
     "'Write a report for each run' to append a report for every run to the report file."},

    {"Calculating the answer",
     "Once data are entered and an equation is chosen, press Fit (F5 or Alt+R).\n\n"
     "Except for the linear equation, the program finds the parameters by non-linear least squares. "
     "The default method is the Marquardt-Levenberg algorithm, which converges quickly under a wide "
     "variety of conditions. In some cases it may not converge properly, so the Gauss-Newton "
     "algorithm is also included. Both give best-fit values \xC2\xB1 1 standard deviation.\n\n"
     "The Results tab rounds each value to the precision its standard deviation supports, for example "
     "Km = 2.71 \xC2\xB1 0.14. Hold the mouse over a value to see all its digits; Copy report and the "
     "written reports keep them too. If you change the data or the settings after a fit, the results "
     "are greyed out until you fit again.\n\n"
     "Gauss-Newton may report 'Local minimum found', meaning that no step could reduce the sum of "
     "squares further within the tolerances; this is not critical but better initial estimates may "
     "give a better fit. 'Maximum of 30 iterations' means no solution was found within 30 iterations, "
     "which usually means the wrong equation was chosen or there are not enough data.\n\n"
     "The fit needs initial estimates of the parameters. Normally the program calculates them from "
     "the data. If those are too far from the answer you can enter your own (see Changing the "
     "minimization type and initial estimates).\n\n"
     "A good fit is one where each standard deviation is less than about 10% of the parameter value "
     "(the Relative SD column). "
     "The program also tests the Hill coefficient against 1.0 with Student's t-test, reports the range "
     "of the data relative to Km, Ka or I50 so that you can judge whether the range was wide enough "
     "(a good rule of thumb is 0.2 Km to 5 Km), and gives the fold activation for the activator "
     "equations. For linear regression it tests the slope against 0 and against 1 and the intercept "
     "against 0.\n\n"
     "You need more data points than parameters + 1; the program warns you otherwise. If you choose "
     "Fit anyway, the data cannot pin the curve down and the fitted values mean little."},

    {"Plotting your data",
     "The Graph tab plots the data and the fitted curve. Choose a plot type from the list. The NORMAL "
     "plot shows [S], [A], [I], X or T against v, Y or log10(v). Only plots that make sense for the "
     "equation are offered: Normal, Log-log and Residual plots for the linear equation; Normal, "
     "Saturation, Job and Residual plots for the inhibition equations; Normal, Arrhenius and Residual "
     "plots for the Arrhenius equation.\n\n"
     "Some plots are included for historical reasons. Lineweaver-Burk (double-reciprocal) and "
     "Eadie-Hofstee plots are not good for estimating kinetic constants (see any book by A. "
     "Cornish-Bowden), but many people are used to them.\n\n"
     "The check boxes control the display, as the space bar did in version 3.51: include the origin "
     "in the axes, extend the curve to the Y axis, and extend it to where it crosses Y = 0 (for "
     "example to -1/Km on a Lineweaver-Burk plot). Points are numbered as in the data and residual "
     "tables. Drag to pan, scroll to zoom, double-click or 'Reset view' to fit the axes, and "
     "right-click for more options.\n\n"
     "The RESIDUAL plot is a graph of the residual (v minus the fitted v, the velocity calculated from "
     "the best-fit parameters) against the fitted v. It shows how well the data fit the equation: a good scatter on both "
     "sides of the axis with no pattern indicates a good fit. If the residuals follow a pattern "
     "(bell shape, inverted bell, ...) another equation may fit better. In general at least 30 points "
     "are needed to justify these tests, but in practice a good scatter can be seen with 8-10 points. "
     "See 'What residuals tell you'."},

    {"What residuals tell you",
     "It is sometimes difficult to decide which equation suits your data. Two non-parametric tests "
     "of the residuals give some support for (or against) the chosen equation; their results are on "
     "the Residuals tab and in the long report.\n\n"
     "The sequence (runs) test asks whether the order of + and - residuals is random. With 8 points "
     "(8 residuals if there are no repeats) the sequence -+-+-+-+ looks random, but is --++---+ "
     "random? The program computes the exact probability of so few runs. At least 8 non-zero "
     "residuals are needed. The sign test asks whether the numbers of + and - residuals are "
     "compatible with chance. Replicates at the same X are averaged before both tests.\n\n"
     "Both tests support the hypothesis that the equation describes the data, but they are by no "
     "means absolute; a complete analysis needs more extensive statistics."},

    {"Using your data as a standard curve",
     "The fitted curve can be used as a standard curve. Enter your data, choose an equation, fit, "
     "then open the Standard curve tab (Calculate > Find [S] for a given v, Alt+X, or Find v for a "
     "given [S], Alt+Y; the names follow the equation's columns).\n\n"
     "New Y values are calculated from the best-fit parameters and the X ([S], [A] or [I]) you enter. "
     "For new X values the equation is inverted and X is calculated the same way. Both are idealised "
     "values read from the fitted curve. A warning is shown when the value is outside the range of "
     "the data."},

    {"Changing the minimization type and initial estimates",
     "You can change the algorithm used to minimise the error and enter your own initial estimates "
     "(Fitting options, below the Fit button, or the Calculate menu).\n\n"
     "If the program has trouble finding the best-fit values with the default Marquardt-Levenberg "
     "method, try Gauss-Newton, which may converge where Marquardt-Levenberg does not.\n\n"
     "With Initial estimates set to Manual, the values in the parameter table are used as starting "
     "values. 'Use automatic estimates' fills them with the program's own estimates, which you can "
     "then adjust. Bounds on each parameter can be shown and edited with 'Show bounds'."},

    {"Writing reports",
     "Reports replace the printed hardcopy of version 3.51. Set the format and destination with "
     "File > Report options (Alt+O) and write a report with File > Write report (Alt+P). The Report "
     "tab shows a preview and can copy the report to the clipboard.\n\n"
     "SHORT gives one line per run:  #  P1  SD1  P2  SD2 ...\n"
     "MEDIUM lists the data and the parameters with their standard deviations.\n"
     "LONG is the complete report: data, parameters, statistics, residuals and residual tests.\n\n"
     "SHORT and MEDIUM can be written comma-separated with quoted headings, ready for a spreadsheet, "
     "or as fixed-width text. The # column is a sequential number (starting from the number you "
     "choose), the run number from the imported file, or nothing.\n\n"
     "Reports are appended to the chosen file, so one file can collect the results of many runs; "
     "choose 'Start a new file' to overwrite it. Print the file from any text editor.\n\n"
     "In the browser version, files are opened with the browser's file picker. Saved tables and report "
     "files are downloaded: each time a report is written, the whole report file collected so far is "
     "downloaded again. Data and files are not kept when the page is closed; only the preferences are."},

    {"Changes from version 3.51",
     "This is a port of SB's Kinetics Program 3.51 (BioTechniques 13:906-911, 1992) to modern C++ "
     "with SDL3 and Dear ImGui. The equations, fitting methods, statistics and file formats are the "
     "same, with these corrections:\n\n"
     "- Automatic analysis no longer skips every other run when reports use sequential numbering, "
     "and the starting number for numbering is now used.\n"
     "- Fitting a single run from a file uses that run's velocity range in the results.\n"
     "- The linear-regression slope is tested against 0 (what v3.51 computed) and against 1 (what "
     "its message claimed); both are now reported.\n"
     "- Marquardt-Levenberg stops on a relative change in chi-square and has an iteration limit "
     "(v3.51 used an absolute change of 0.1, so data with small values stopped too early).\n"
     "- Gauss-Newton clamps the correct parameter at its bound when fixing a parameter reorders "
     "the list, tests convergence per parameter, and reports the true iteration count.\n"
     "- The Arrhenius derivative used 2.3036 instead of ln 10 = 2.3026 and the gas constant varied "
     "between routines; the initial estimate now uses a straight-line fit of log10(v) against 1/T. "
     "Temperatures use 273.15 K, and log10(A) may be negative.\n"
     "- The extrapolated curves of the Hill and log-log plots no longer transform the axis "
     "intercept twice.\n"
     "- The runs test handles all sequence lengths; the '> 2 S.D.' flags of linear regression use "
     "its own residual standard deviation.\n"
     "- Calculations use double precision, and all memory handling is automatic.\n\n"
     "New in this version: any parameter can be fixed, bounds are editable, the iteration history "
     "is shown, data tables can be saved and pasted, and reports go to text files instead of LPT1. "
     "View > Text size (Ctrl+= and Ctrl+-) makes the text larger or smaller, and View > Theme chooses "
     "a light or dark theme; by default it follows the system."},
};

}  // namespace

std::span<const HelpTopic> help_topics() { return kTopics; }

}  // namespace app
