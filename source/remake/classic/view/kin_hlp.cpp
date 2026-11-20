// Port of KIN_HLP.CPP: the HELP menu and its nine topics, shown with the
// d_data pager. The topic text is copied from v3.51's KIN_HLP.CPP
// (by script, and checked against it) as it was, spelling included;
// "v3.51" comments note what was kept on purpose.

#include "classic/d_data.hpp"
#include "classic/kinetics.hpp"
#include "classic/view/pager.hpp"

namespace classic {
namespace {

// The loop each topic of v3.51 repeated: page through the text until ESC
// (back to the topic menu, 0) or an Alt key (a menu, -1).
Task<int> show_topic(const char s[][80], int ne, int& ch) {
    int lista[80] = {}, colour[80], START = 1, item = 1, special = NO;
    d_data dd(2, 5, 80, 23, BLUE, YELLOW);
    for (int i = 0; i <= ne; i++) colour[i] = WHITE;
    for (;;) {
        ch = co_await display_wait(dd, s, colour, lista, ne, START, item, special);
        if (ends_list(ch, special)) break;
    }
    if (special) co_return -1;  // an Alt key
    else co_return 0;
}

Task<int> help_ce(int& ch) {
    static const char s[57][80] = {
        "CHOOSING AN EQUATION:",
        "    You may select one of several different types of equations listed under",
        "this option. The Michaelis-Menten and HILL equations describe single-",
        "substrate reactions. The HILL equation allows for cooperativity between",
        "enzyme subunits to produce velocity vs. [S] curves which are sigmoidal rather",
        "than square hyperbolic (as seen with the Michaelis-Menten equation). Two",
        "different HILL equations can be selected: h>0 or h≥1. The h>0 option allows",
        "one to fit enzyme mechanisms which show negative cooperativity. The h≥1",
        "option will allow you to set a lower limit of 1.0 to the Hill coefficient.",
        "Use this option only when you are certain that your enzyme shows positive",
        "cooperativity. It is normal to select the h>0 option first and test if",
        "the Hill coefficient is significantly different from 1.0 (this test is",
        "included in the calculations).",
        " ",
        "     Three ACTIVATION equations are available. These are equivalent to the",
        "three equations discussed above (Michaelis-Menten and HILL) with the",
        "exception that the velocity is NOT zero in the absence of activator. As",
        "stated above, one should begin by selecting the ACTIVATION (h>0) option and",
        "then switch to using the h≥1 or h=1 options if the data dictates that these",
        "options can be used. The h=1 option is equivalent to a Michaelis-Menten",
        "mechanism with a finite velocity at 0 [S].",
        "  ",
        "     Three INHIBITION equations are also available. The linear inhibition",
        "equation is defined as: ",
        " ",
        "          v = v0*I50/(I50 + [I])",
        " ",
        "where the I50 value is the concentration at which v = v0/2. This is the ",
        "simplest inhibition equation and should be essayed first. The non-linear I50",
        "equation also returns the I50 value but is designed to fit curves that do not",
        "conform to the linear equation. As such the non-linear equations (both I50",
        "and K50) are empirical equations based on the HILL equation. These equations,",
        "therefore, also return a HILL coefficient as a measure of inhibitor",
        "cooperativity (although this paramter has no physical meaning). Note that the",
        "K50 equation returns the mid-point of the inhibition curve, defined as the",
        "concentration at which v = (v0-vmin)/2+vmin.",
        "  ",
        "     Two other equations are also supplied for convienence: the LINEAR and",
        "ARRHENIUS equations. The linear curve is self-explanatory. The ARRHENIUS",
        "equation is as follows: ",
        "  ",
        "          log10(v) = log10(A) - Ea/R/(T (°C) + 273.2)",
        " ",
        "     Note that the arrhenius equation requires you to enter log (base 10)",
        "velocity values and temperature in degrees centigrade. When in doubt use",
        "the headings given in the top of the ADD/EDIT DATA section as a guide.",
        "  ",
        "     Finally note that you can fix certain parameters if you so desire. For",
        "example, you can fix the maximal velocity when the Michaelis-Menten equation",
        "is selected. This option exists for convenience only. Your data curve should",
        "span a sufficiently large range so that the program can easily determine the",
        "parameter values corresponding to the error function minimum. However if you",
        "have several measurements at a high substrate concentration, and consequently",
        "know the maximal velocity for the enzyme, you can opt to fix the Vmax value",
        "at a constant (known) value.  Other parameters may be held fixed when other",
        "equations are selected but take care that you have accurately determined the",
        "value of the fixed parameter."
    };
    co_return co_await show_topic(s, 56, ch);
}

Task<int> help_edit(int& ch) {
    static const char s[19][80] = {
        "ADDING/EDITING DATA",
        "     Substrate concentrations [S], activator concentrations [A], inhibitor",
        "concentrations [I], X values (X) or temperatures (T °C) are entered in the",
        "left column while corresponding velocity values (v), Y values (Y), or base",
        "10 logarithms of velocity (log10(v)) values are entered in the right column.",
        "Type in the correct entry (use the backspace to erase any mistakes when",
        "entering values) and press return or a DIRECTION key (arrow keys on the",
        "number keypad). To erase previous values you may place the cursor over the",
        "value and press delete. To overwrite values press the INS key to toggle the",
        "insert function off. You can now write over previous values. Note that with",
        "the INS function off you cannot add any new values to the list. To add new",
        "values, go to the end of the column (press END), make sure that the INS flag",
        "is present in the lower right hand side of the screen, and enter your new",
        "values as before. Be sure to enter the correct values (pay attention to the",
        "individual column headings) otherwise you may get some funny results!",
        " ",
        "     Note that the computer automatically orders your data from lowest to",
        "highest substrate value. This greatly simplifies some of the calculations",
        "which the computer must do and is, therefore, done for convenience only."
    };
    co_return co_await show_topic(s, 18, ch);
}

Task<int> help_file(int& ch) {
    // v3.51: there is no comma after "...below the last entry.", so that
    // line and the blank one after it are one (written here as one).
    static const char s[68][80] = {
        "ENTERING/USING FILE DATA",
        "     You can analyse data that has been saved as a file.  The file should be",
        "an ASCII text file and formatted as shown below.  You can easily create data",
        "files with LOTUS 1-2-3 and QuattroPro by first entering the data in the ",
        "format shown below using a spreadsheet program.  Next, print the data matrix",
        "to a file using the spreadsheet program (not to the printer).  Now quit the",
        "spreadsheet program and start the KINETICS program.  To use the file data you",
        "must first tell the KINETICS program which file you want to use as a source",
        "of data.  Do this by choosing one of the IMPORT functions found under the file",
        "menu heading.  You can now view each file entry and edit the data by selecting",
        "the 'EXAMINE FILE DATA' option under the EDIT command.  This, however, is",
        "not required to calculate the parameters. ",
        " ",
        "The following is an example of the correct format for a spreadsheet data file:",
        " ",
        "S=NS",
        "#     S1      S2      S3      S4      S5     S6     S7",
        "RN1   v11     v12     v13     v14     v15    v16    v17",
        "RN2   v21     v22     v24     v25     v25    v26    v27",
        "RN3   v31     v32     v33  ....",
        "....",
        "#",
        " ",
        "Where NS is the number of different substrate columns (for example, the",
        "first line in the above file should read S=7).  Note that the 'S=NS'",
        "statement contains NO spaces between 'S', '=' and the NS terms. ",
        "If the file is not in the exact format you will get an 'Incorrect file",
        "format' error message.  The '#' sign is obligatory as it sets the top",
        "left corner of the file.  The '#' is followed by a list of substrate",
        "concentrations (S1 S2 S3 ....) all on the same row and separated by BLANK",
        "spaces, commas or tabs.  There must be equivalence between 'NS' (in the",
        "S=NS statement) and the # of substrate values in the data matrix.  Directly",
        "beneath the '#' sign you must enter the run number (RN).  Therefore, you",
        "specify which set of velocity-substrate pairs to analyse by entering the",
        "appropriate RN (Run #) value when the computer prompts you.  By entering",
        "a '1' when the computer prompts you for the Run # to analyse, you will",
        "cause the computer to automatically load the following data pairs: ",
        "[S1,v11], [S2, v12], [S3, v13], ....  You can omit individual substrate",
        "values from a particular data set by setting the velocity corresponding",
        "to the omitted [substrate] equal to '-1' in the original data file.  For",
        "example, if you do not have a velocity measurement for S1 from Run #1 but",
        "do have velocities for S1 from other runs, you would set v11 = -1 in the",
        "table.  The computer will omit S1 from the calcuations for RN #1.  Other",
        "runs will not be affected by this procedure.  It is important to add a '#'",
        "sign after the last velocity value to tell the program that the file has",
        "ended.  Place this sign on a new line below the last entry.     ",
        "     An MPA data file is one produced by the MicroPlate Analysis program",
        "which is also available from S. Brooks.  This program analyzes microplate",
        "data and prints a table of cell # versus slope (velocities of individual",
        "cells.  In this case you must tell the computer what record number to start",
        "(the first record number containing velocity data) and how many records to",
        "import.  The computer does not import [S] data from this file and you must",
        "enter the [S] values using the EDIT function.",
        " ",
        "     Three different CALCULATION options are available with FILE data.",
        "(1) AUTOMATIC PREVIEW (DOES NOT PRINT) lets you examine the data prior to",
        "printing a hardcopy to let you see if the data is adequately modeled by the",
        "equation you have chosen.  This option calculates the parameters and then",
        "plots the dat.  You simply press a key and the next data set is processed.",
        "Before you select either the PARAMETERS FROM DATA FILE: SINGLE or the",
        "PARAMETERS FROM DATA FILE: AUTOMATIC options you should choose your PRINTER",
        "OPTIONS (under the FILE command of the main menu).  The AUTOMATIC option",
        "asks you for the number of the first record and then automatically calculates",
        "parameters for all following data sets until it comes to the end of the file.",
        "At this point you will see an error message saying: \"Run # not found or End",
        "of File encountered\".  Simply press ESC to return to the menu.  Remember: If",
        "you use the automatic analysis feature you should be sure to have the computer",
        "automatically print out a report of each run analysis."
    };
    co_return co_await show_topic(s, 67, ch);
}

Task<int> help_calculate(int& ch) {
    static const char s[38][80] = {
        "CALCULATING YOUR ANSWER:",
        "     Once data are entered and an equation type is chosen you may proceed to ",
        "calculate your data. With the exception of the LINEAR equation, this program",
        "determines optimal values of parameters by non-linear least squares",
        "regression. The default method for determining these parameters is the",
        "Marquardt-Levenberg algorithm which quickly converges under a variety of",
        "conditions. In some instances this algorithm may not converge properly and",
        "so the (marginally) slower Gauss-Newton algorithm has also been included.",
        "In both cases best fit values are provided ± 1 standard deviation. When the",
        "Gauss-Newton algorithm is used, error messages may appear. 'LOCAL MINIMUM",
        "FOUND' indicates that the algorithm could not find a function minimum within",
        "the tolerances set for the minimization. This is not critical but may",
        "indicate that better initial guesses could provide a better final fit.",
        "'MAXIMUM OF 30 ITERATIONS' indicates that the algorithm could not find best",
        "fit values (within the tolerances set) after 30 iterations. This usually",
        "indicates that you have chosen the wrong equation for your data or that you",
        "have not given the computer enough data to minimize properly.",
        " ",
        "     Note that the minimization process requires initial guesses of the",
        "paramters. During normal operation the computer generates these values based",
        "the data. In some instances the computer-generated initial estimates are",
        "too far from the best-fit minimum parameter value to permit optimal parameter",
        "minimization. In this case you can opt to enter the initial parameters",
        "yourself, and in this way provide better initial guesses to improve parameter",
        "estimation. If you selected either an ACTIVATION or INHIBITION equation and",
        "selected the FIXED v0 option, the computer will first look to see if you",
        "entered a velocity that corresponds to [A]=0 or [I]=0. If you have, it will",
        "use this value as the FIXED v0 value.",
        "  ",
        "      A good fit is one where the standard deviation is less than 10% of the",
        "value of the paramter.  The computer also performs statistical tests on some",
        "of the parameters. For example, a Student's t-test is used to determine if",
        "the Hill coefficient is significantly different from 1.0. The computer also",
        "provides you with the range of values used (with respect to the Km, Ka or I50",
        "values) so that you can determine if you have chosen a sufficiently wide ",
        "substrate range (a good rule of thumb is that your data should vary from",
        "0.2Km to 5Km). The fold activation is provided when the ACTIVATION equations",
        "are selected."
    };
    co_return co_await show_topic(s, 37, ch);
}

Task<int> help_plot(int& ch) {
    static const char s[28][80] = {
        "PLOTTING YOUR DATA:",
        "     Now that you have calculated your data you will naturally want to",
        "see how it looks. Several different plots are available for this purpose.",
        "The NORMAL option simply plots [S], [A], [I], X, or T versus velocity, the Y",
        "value or log10(v) respectively. Note that certain plots do not work when some",
        "equations are chosen. For example, you may only select the NORMAL, LOG-LOG,",
        "or RESIDUAL plot when you use the LINEAR equation. Similarly, you can only",
        "select the NORMAL, INHIBITION, or RESIDUAL plot when you select an INHIBITION.",
        "equation. If a plot cannot be selected the program simply refuses to carry",
        "out the instruction. Some plots are included only for historical reasons. For",
        "example, the LINEWEAVER-BURK and EADIE-HOFSTEE plots are not especially good",
        "for determining kinetic constants (see any book by A. Cornish-Bowden) but",
        "most people are used to plotting data in one of these formats so they are",
        "provided here for completeness.",
        " ",
        "      The RESIDUAL plot is a graph of velocity-vhat (vhat is the velocity",
        "calculated using the fitted paramters and the initial [S] values) as a ",
        "function of vhat. It is useful for determining how well your data fit the",
        "chosen equation. A good scatter on either side of the x-axis with no",
        "discernable pattern to the residuals indicates a good fit. If the",
        "residuals follow a defined pattern (bell shape, inverted bell, ...) the",
        "data may be best fit by another equation. Note that, in general, at least",
        "30 data points are required to justify use of these tests. However, in ",
        "practice one can see a good scatter with as few as 8-10 points.  Naturally,",
        "one cannot sit all day at the spectrophotometer collecting enough data to",
        "perform all statistical tests so a balance between the amount of data and ",
        "the reliability of the tests must be maintained. See WHAT RESIDUALS TELL YOU",
        "for more information."
    };
    co_return co_await show_topic(s, 27, ch);
}

Task<int> help_residuals(int& ch) {
    static const char s[18][80] = {
        "WHAT RESIDUALS TELL YOU:",
        "     It is sometimes difficult to determine the correct type of equation",
        "to use with your particular data set. In most cases, you know the",
        "equation that best fits your data. However, it is often nice to have a",
        "statistical test to confirm this assumption. Two such tests are provided",
        "in the present program: a sequence test and a random # test. The sequence",
        "test tests the sequence of + and - residuals to determine if it is random.",
        "For example, if you have 8 data points (if no repeats this corresponds to ",
        "8 residuals), a residual sequence of -+-+-+-+ is fairly indicative of a",
        "random distribution of residuals. However, is the sequence --++---+ random?",
        "In order to answer this question, the randomness of the residuals is tested",
        "using a non-parametric method and the results are reported. The number of",
        "+ and - values is also tested for randomness using another non-parametric",
        "test. The results of these tests are reported at the bottom of the RESIDUALS",
        "screen. Although both tests provide some degree of support for the hypothesis",
        "that the equation accuartely describes the data, they are by no means absolute",
        "tests. Extensive statistical tests (not inculded with the present program",
        "would have to be perfomed for a complete analysis."
    };
    co_return co_await show_topic(s, 17, ch);
}

Task<int> help_get(int& ch) {
    static const char s[10][80] = {
        "USING YOUR DATA AS A STANDARD CURVE:",
        "     It is possible to use the data, and resulting paramters as a standard",
        "curve to quantify a response. To do this simply enter your data, choose",
        "an equation, calculate the parameters and then select either: \"NEW X VALUE",
        "FOR ENTERED Y VALUE\" OR \"NEW Y VALUE FOR ENTERED X VALUE\" under the",
        "CALCULATE menu command.  The new Y values will be calculated using the best",
        "fit parameters and the entered X ([S], [A], or [I]) value. In order to",
        "obtain new X values, the equation is inverted and X is calculated as before",
        "using the bet fit parameters. Thus the new Y and new X values are idealized",
        "values obtained from the fitted curve. "
    };
    co_return co_await show_topic(s, 9, ch);
}

Task<int> help_options(int& ch) {
    static const char s[15][80] = {
        "CHANGING THE MINIMIZATION TYPE AND INITIAL ESTIMATES:",
        "     It is possible to both (i) change the algorithm used to minimize",
        "the error and (ii) enter your own initial parameter estimates. One may wish",
        "to change the minimization algroithm to improve program performance in",
        "certain instances. As indicated in the section on CALCULATING YOUR ANSWER,",
        "the program may, from time to time, have trouble finding the best fit",
        "parameter values. If this occurs you may elect to change the minimization",
        "type (default = Marquardt-Levenberg) to the Gauss-Newton algorithm which may",
        "converge in instances where the ML algorithm does not. You may also wish to",
        "enter your own parameter estimates. As indicated in the section on",
        "CALCULATING YOUR ANSWER, the program requires initial estimates of the",
        "parameters. The program can either calculate them directly from your data or",
        "can make use of use-supplied estimates. In cases where the program calculated",
        "estimates are far from the real parameter values user-supplied estimates may",
        "allow the program to converge to a solution."
    };
    co_return co_await show_topic(s, 14, ch);
}

Task<int> help_print(int& ch) {
    static const char s[32][80] = {
        "PRINTING YOUR DATA:",
        "     Now that you have calculated your data you may wish to obtain a",
        "hardcopy of the results.  Note that the destination and formatting of",
        "the hardcopy is controlled by the \"Printer Options\" option found under",
        "the \"File\" command of the main menu.",
        " ",
        "     The formatting can be set to either SHORT, MEDIUM or LONG.  The",
        "SHORT format consists of a single line as shown below: ",
        " ",
        "     #     P1     SD1     P2     SD2 ...",
        " ",
        "Where # is the file number (if the numbering option = FILE NUMBER) or a",
        "sequential number (if the numbering option = SEQUENTIAL).  The MEDIUM ",
        "format produces a list of the [S] and v values as well as a list of the",
        "parameters and their standard deviations.  Both the SHORT and MEDIUM ",
        "formats can produce FILES (see below) that are ready for importation into",
        "spreadsheet programs (use the comma and quotation mark delimited option under",
        "the spreadsheet import command).  The LONG format produces a complete report",
        "of the data including all [S] and v values,  all parameters and their",
        "standard deviations as well as all residuals and residual tests.  The",
        "LONG format cannot produce FILES that are ready for spreadsheet importation.",
        " ",
        "     You can elect to send the output either to a FILE or to the PRINTER.",
        "If you send the hardcopy to a FILE, the program will prompt you for a file",
        "name after you \"Quit and save\" the Printer options.  If you send your",
        "output to the PRINTER, the program will check to see if the printer is, ",
        "in fact, connected to your computer via the LPT1 port.  If your printer is",
        "not attatched to LPT1, you have two options.  You may either: (1) type ",
        "\"MODE LPT1=YOUR PRINTER PORT\" (using DOS 5) at the DOS prompt before you",
        "start the program (see your DOS mannual for info on the MODE command) or ",
        "(2) you can print the data to a file which may then be printed from DOS or",
        "from a word processing program."
    };
    co_return co_await show_topic(s, 31, ch);
}

}  // namespace

Task<int> help_option(int& ch) {
    screen help(1, 4, 80, 23, BLUE);
    int ans = 1;
    // v3.51: "Changing the minimization type..." has the same letter as
    // "Calculating the answer" (C), which is found first; it can only be
    // chosen with the arrow keys and RETURN.
    const int pos[] = {0, 12, 15, 9, 0, 1, 5, 0, 0, 0};
    const char* const s[] = {"choosing an Equation", "adding/editing Data",
                             "entering File data", "Calculating the answer",
                             "pLotting your data", "what Residuals tell you",
                             "Using your data as a standard curve",
                             "Changing the minimization type and initial estimates",
                             "Printing your results to the printer"};

    help.put_str(3, 3, "SELECT A HELP TOPIC", YELLOW);
    single_response_window srw(0, 0, 0, 0, s, pos, 0, 9, 1, USER_DEF, BLUE, YELLOW, WHITE, LIGHTRED);

    for (;;) {
        help.print_screen();
        // v3.51: after a topic, ans is 0, so the menu comes back with no
        // highlight and only the letter keys choose a topic.
        ans = co_await srw.get_single_response(ans, ch);
        if (ans <= 0) {
            srw.show_window(0, WHITE);
            co_return ans;
        } else if (ans == 1) ans = co_await help_ce(ch);
        else if (ans == 2) ans = co_await help_edit(ch);
        else if (ans == 3) ans = co_await help_file(ch);
        else if (ans == 4) ans = co_await help_calculate(ch);
        else if (ans == 5) ans = co_await help_plot(ch);
        else if (ans == 6) ans = co_await help_residuals(ch);
        else if (ans == 7) ans = co_await help_get(ch);
        else if (ans == 8) ans = co_await help_options(ch);
        else if (ans == 9) ans = co_await help_print(ch);
        if (ans < 0) co_return -1;
    }
}

}  // namespace classic
