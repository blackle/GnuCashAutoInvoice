#include "pdf_builder.hpp"
#include <iostream>
#include <cstdlib>

#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

#include "gnucash/gnc-report.h"
#include "swig-runtime.h"

#define scm_is_proccedure(x) (scm_is_true(scm_procedure_p(x)))

#define INVOICE_REPORT_NAME "QCC Printable Invoice"
#define INVOICE_REPORT_GUID "8d9d7e592dc948f3af139c9411f7c124"

struct PDFBuilderPrivate {
	QofBook* book = nullptr;
	SCM make_report_func = 0;
};

PDFBuilder::PDFBuilder(QofBook* book) {
	p = std::make_shared<PDFBuilderPrivate>();
	p->book = book;
}

//todo: use more functional programming techniques to return error information
#define PDF_RETURN(line) do { \
	std::cerr << "PDF_RETURN error on line " << line << std::endl; \
	return false; \
} while(0)

bool PDFBuilder::init() {
	p->make_report_func = scm_c_eval_string ("gnc:invoice-report-create");
	if (scm_is_proccedure(p->make_report_func) == false) PDF_RETURN(__LINE__);

	return true;
}

std::optional<std::string> generateHtmlForInvoice(GncInvoice* invoice, SCM make_report_func) {
	SCM args = SCM_EOL;
	SCM arg = SWIG_NewPointerObj(invoice, SWIG_TypeQuery("_p__gncInvoice"), 0);
	SCM arg2 = scm_from_utf8_string(INVOICE_REPORT_GUID);
	args = scm_cons2 (arg, arg2, args);
	arg = scm_apply (make_report_func, args, SCM_EOL);
	if (scm_is_exact (arg) == false) {
		std::cerr << "scm_is_exact (arg) == false" << std::endl;
		return std::nullopt;
	}
	int report_id = scm_to_int (arg);

	char *html, *errmsg;
	if (gnc_run_report_with_error_handling(report_id, &html, &errmsg)) {
		return html;
	} else {
		std::cerr << errmsg << std::endl;
		return std::nullopt;
	}
}

#define INPUT_END 1
#define OUTPUT_END 0
#define CONVERTER_COMMAND "wkhtmltopdf"
static void renderPDF(const std::string& html, const std::string& filename) {
	int pipes[2];
	pipe(pipes);

	int pid = fork();
	if (pid != 0) {
		close(pipes[OUTPUT_END]);
		write(pipes[INPUT_END], html.c_str(), html.length());
		close(pipes[INPUT_END]);

		int status;
		waitpid(pid, &status, 0);
	} else {
		close(pipes[INPUT_END]);
		dup2(pipes[OUTPUT_END], STDIN_FILENO);
		close(pipes[OUTPUT_END]);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);

		char* const arguments[] = {
			strdup(CONVERTER_COMMAND),
			strdup("--page-size"),
			strdup("A5"),
			strdup("-"),
			strdup(filename.c_str()),
			nullptr
		};

		exit(execvp(CONVERTER_COMMAND, arguments));
	}
}

void PDFBuilder::renderInvoice(GncInvoice* invoice, const std::string& filename) {
	auto html = generateHtmlForInvoice(invoice, p->make_report_func);
	if (!html.has_value()) {
		std::cerr << "WARNING, COULDN'T GENERATE INVOICE " << filename << std::endl;
		return;
	}
	renderPDF(html.value(), filename);
}
