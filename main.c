#include "error.h"
#include <ctype.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define	MAX_NUMBER_OF_TOKENS_PER_CELL	256
#define	MAX_LENGTH_FOR_CELL_CONTENT	32

enum TokenKind {
	tkind_string		= '"',
	tkind_copy_top		= '^',
	tkind_copy_bottom	= 'v',
	tkind_copy_left		= '<',
	tkind_copy_right	= '>',
	tkind_reference		= '@',
	tkind_minus_sign	= '-',
	tkind_add_sign		= '+',
	tkind_mul_sign		= '*',
	tkind_div_sign		= '/',
	tkind_math_expr		= '=',
	tkind_number		= 255,
	tkind_unknown,
};

enum CellKind {
	ckind_error	= 0,
	ckind_number,
	ckind_text,
	ckind_copy,
};

struct Reference {
	unsigned int row;
	unsigned int col;
};

struct Token {
	union {
		long double number;
		char *text;
		struct Reference ref;	
	} as;

	char		*context;
	size_t		textlen;
	enum TokenKind	kind;
};

struct Cell {
	char		final[MAX_LENGTH_FOR_CELL_CONTENT];
	unsigned short	width;
	enum CellKind	kind;
};

struct Sheet {
	struct Cell	*cells;
	char		*sheet;
	size_t		bytes;
	unsigned short	nrows;
	unsigned short	ncols;
};

/*  ____________________________________
 * / error for cells; does not kill the \
 * \ program!                           /
 * ------------------------------------
 *        \   ^__^
 *         \  (@@)\_______
 *            (__)\       )\/\
 *                ||----w |
 *                ||     ||
 */
enum CellErrorKind {
	ckerr_empty	= 0,
};

struct CellError {
	char	*error;
	size_t	len;
};

static size_t read_file (FILE *const, char **const);
static void get_sheet_size (struct Sheet *const);

static void start_table_analysis (struct Sheet *const);
static enum TokenKind kind_of_this (const char, const char);

static size_t get_string_token (const char*, size_t*, unsigned short*, unsigned short*);
static size_t get_reference_token (const char*, struct Reference*, unsigned short*, unsigned short*);

static void set_cell (struct Cell *const, struct Token *const, const unsigned short);
static void set_cell_as_error (struct Cell *const, const enum CellErrorKind);

int main (int argc, char **argv)
{
	if (argc == 1) e_print_usage();

	FILE *file = fopen(argv[1], "r");
	if (!file) e_system("cannot read file");

	struct Sheet sheet;
	memset(&sheet, 0, sizeof(struct Sheet));

	sheet.bytes = read_file(file, &sheet.sheet);
	get_sheet_size(&sheet);

	sheet.cells = (struct Cell*) calloc(sheet.ncols * sheet.nrows, sizeof(struct Cell));
	e_check_ptr(sheet.cells, "table way too big");

	start_table_analysis(&sheet);
	return 0;
}

static size_t read_file (FILE *const file, char **const mem)
{
	fseek(file, 0, SEEK_END);
	const size_t bytes = ftell(file);
	fseek(file, 0, SEEK_SET);

	*mem = (char*) calloc(bytes + 1, sizeof(char));
	e_check_ptr(mem, "file's content");

	const size_t leidos = fread(*mem, 1, bytes, file);
	if (leidos != bytes)
		fprintf(stderr, "\t[s4Term:warning]: not whole file was read: %ld/%ld (B)\n", leidos, bytes);

	fclose(file);
	return bytes;
}

static void get_sheet_size (struct Sheet *const sheet)
{
	unsigned short nmaxcol = 0;
	for (register size_t k = 0; k < sheet->bytes; k++) {
		const char c = sheet->sheet[k];
		if (c == 10) {
			nmaxcol = (sheet->ncols > nmaxcol) ? sheet->ncols : nmaxcol;
			sheet->ncols = 0;
			sheet->nrows++;
		}
		else if (c == '|') sheet->ncols++;
	}
	sheet->ncols = nmaxcol;
}

static void start_table_analysis (struct Sheet *const sheet)
{
	struct Token this_expr[MAX_NUMBER_OF_TOKENS_PER_CELL];

	unsigned short l_offset = 1;
	unsigned short numline  = 1;
	unsigned short tkexpr_i	= 0;
	unsigned short n_tokens = 0;

	struct Cell *ths_cell = &sheet->cells[0];

	for (size_t k = 0; k < sheet->bytes; k++) {
		const char c = sheet->sheet[k];

		if (isspace(c)) {
			if (c == 10) {
				ths_cell = &sheet->cells[numline++ * sheet->ncols];
				l_offset = 0;
			}
			l_offset++;
			continue;
		}

		if (c == '|') {
			set_cell(ths_cell, this_expr, n_tokens);
			memset(this_expr, 0, sizeof(struct Token) * MAX_NUMBER_OF_TOKENS_PER_CELL);

			ths_cell++;
			l_offset++;

			tkexpr_i = 0;
			n_tokens = 0;
			continue;
		}

		if (tkexpr_i == MAX_NUMBER_OF_TOKENS_PER_CELL)
			e_at_lexing("token overflow (max is %d)", sheet->sheet + k, numline, l_offset, MAX_NUMBER_OF_TOKENS_PER_CELL);

		struct Token *thstk = &this_expr[tkexpr_i++];
		thstk->kind = kind_of_this(c, sheet->sheet[k + 1]);
		thstk->context = sheet->sheet + k;

		switch (thstk->kind) {
			case tkind_unknown:
				e_at_lexing("unknown token", thstk->context, numline, l_offset);
				break;

			case tkind_number: {
				char *ends;
				thstk->as.number = strtold(thstk->context, &ends);
				const size_t advc = ends - thstk->context - 1;
				k += advc;
				l_offset += (unsigned short) advc;
				break;
			}

			case tkind_string: {
				thstk->as.text = thstk->context;
				thstk->textlen = get_string_token(++thstk->context, &k, &l_offset, &numline);
				break;
			}

			case tkind_reference: {
				const size_t advc = get_reference_token(thstk->context, &thstk->as.ref, &numline, &l_offset);
				k += advc;
				l_offset += (unsigned short) advc;
				break;
			}
		}

		l_offset++;
		n_tokens++;
	}
}

static enum TokenKind kind_of_this (const char a, const char b)
{
	switch (a) {
		case '"': case '^':
		case 'v': case '<':
		case '>': case '@':
		case '+': case '*':
		case '/': case '=': return a;
	}

	if (a == '-')
		return isdigit(b) ? tkind_number : tkind_mul_sign;
	return isdigit(a) ? tkind_number : tkind_unknown;
}

static size_t get_string_token (const char *context, size_t *k, unsigned short *l_off, unsigned short *nline)
{
	size_t len = 0;

	while (*context != '"' && *context) {
		context++;
		len++;

		if (*context == '\n')
			e_at_lexing("multiple line string not allowed", context - len - 1, *nline, *l_off - len);
	}

	*k += len + 1;
	*l_off += len + 1;
	return len;
}

static size_t get_reference_token (const char *context, struct Reference *ref, unsigned short *nline, unsigned short *l_off)
{	
	size_t b_used = 0;
	if (!isalpha(*(context + 1))) goto bad_ref;

	++context;
	b_used++;

	unsigned short level = 0;
	while (isalpha(*context)) {	
		ref->col += (level * 26) + (tolower(*context) - 'a');	
		context++;
		b_used++;
		level++;
	}

	if (!isdigit(*context)) goto bad_ref;

	char *ends;
	ref->row = (unsigned int) strtold(context, &ends);	

	b_used += ends - context - 1;
	return b_used;

	bad_ref:
	e_at_lexing("bad reference definition", context - b_used, *nline, *l_off);
	return 0;
}


static void set_cell (struct Cell *const cell, struct Token *const tokens, const unsigned short n_tkns)
{
	if (!n_tkns) {
		set_cell_as_error(cell, ckerr_empty);
		return;	
	}
}

static void set_cell_as_error (struct Cell *const cell, const enum CellErrorKind wh)
{
	static const struct CellError errors[] = {
		{"!empty-cell", 12}
	};
	snprintf(cell->final, errors[wh].len, "%s", errors[wh].error);
	cell->kind = ckind_error;
	printf("%s\n", cell->final);
}

