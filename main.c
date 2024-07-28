#include "error.h"
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <getopt.h>
#include <stdlib.h>

#define	MAX_TEXT_LENGTH		128
#define	MAX_TOKENS_PER_CELL	32

enum TokenType {
	ttype_next_cell 	= '|',
	ttype_text			= '"',
	ttype_reference 	= '@',
	ttype_expression	= '=',
	ttype_clone_up		= '^',
	ttype_clone_left	= '<',
	ttype_clone_right	= '>',
	ttype_clone_down	= 'v',
	ttype_sub_sign		= '-',
	ttype_add_sign		= '+',
	ttype_mul_sign		= '*',
	ttype_div_sign		= '/',
	ttype_is_space		= 128,
	ttype_is_newline	= 129,
	ttype_is_number		= 130,
	ttype_is_unknonw	= 131
};

enum CellType {
	ctype_error_empty	= 0,
	ctype_error_nosense	= 1,

	ctype_number		= 50,
	ctype_text			= 51,
	ctype_clone_up		= 52,
	ctype_clone_down	= 53,
	ctype_clone_left	= 54,
	ctype_clone_right	= 55
};

union Value {
	struct			{ char *src; unsigned short len; } text;
	struct			{ unsigned short col; unsigned short row; } reference;
	long double		number;
};

struct Token {
	union Value		as;
	enum TokenType	type;
};

struct Cell {
	union Value		as;
	struct Token	*expression;
	unsigned short	expr_len;
	enum CellType	type;
};

struct Lexer {
	char			*src;
	size_t			t_bytes;
	size_t			at;
	unsigned short	numline;
	unsigned short	linepos;
};

struct SheetInfo {
	struct Lexer	lexer;
	struct Cell		*grid;
	char			*in_filename;
	char			*out_filename;
	unsigned int	t_cells;
	unsigned short	rows;
	unsigned short	cols;
};

struct ErrCell {
	char	*error;
	size_t	length;
};

static void read_file (const char *const, struct Lexer *const);
static void get_table_dimensions (const char*, unsigned short*, unsigned short*);

static void analyze_table (struct SheetInfo *const);
static enum TokenType que_es_esto (struct Lexer *const);

static void get_token_as_a_string (union Value *const, struct Lexer *const);
static void get_token_as_a_number (long double *const, struct Lexer *const);

static void get_token_as_a_reference (union Value *const, struct Lexer *const);
static void solve_cell (struct SheetInfo *const, struct Cell *const, struct Token *const);

static void set_error_on_cell (struct Cell *const, const enum CellType);
static void print_outsheet (const struct SheetInfo *const);

int main (int argc, char **argv) {
	if (argc == 1) error_usage();

	struct SheetInfo sheet;
	memset(&sheet, 0, sizeof(struct SheetInfo));

	int op;
	opterr = 0;

	while ((op = getopt(argc, argv, ":s:o:h")) != -1) {
		switch (op) {
			case 's': sheet.in_filename  = optarg; break;
			case 'o': sheet.out_filename = optarg; break;
			default: error_usage();
		}
	}

	read_file(sheet.in_filename, &sheet.lexer);
	get_table_dimensions(sheet.lexer.src, &sheet.rows, &sheet.cols);


	sheet.t_cells = sheet.rows * sheet.cols;
	sheet.grid    = (struct Cell*) calloc(sheet.t_cells, sizeof(struct Cell));
	error_check_ptr(sheet.grid);

	analyze_table(&sheet);
	print_outsheet(&sheet);

	return 0;
}

static void read_file (const char *const filename, struct Lexer *const lexer)
{
	if (!filename) error_usage();
	FILE *file = fopen(filename, "r");

	if (!file)
		error_fatal("`%s' sheet given does not work", filename);

	fseek(file, 0, SEEK_END);
	lexer->t_bytes = ftell(file);
	fseek(file, 0, SEEK_SET);

	lexer->src = (char*) calloc(lexer->t_bytes + 1, sizeof(char));
	error_check_ptr(lexer->src);

	const size_t r_bytes = fread(lexer->src, 1, lexer->t_bytes, file);
	if (r_bytes != lexer->t_bytes)
		error_fatal("cannot read whole file, only %ld out of %ld bytes were read", r_bytes, lexer->t_bytes);
	fclose(file);
}

static void get_table_dimensions (const char *s, unsigned short *r, unsigned short *c)
{
	unsigned short maxc = 0;
	while (*s) {
		const char ch = *s++;
		if (ch == '\n') {
			maxc = (*c > maxc) ? *c : maxc;
			*r += 1;
			*c = 0;
		} else if (ch == '|') *c += 1;
	}

	*c = maxc;
}

static void analyze_table (struct SheetInfo *const sheet)
{
	struct Token tokens[MAX_TOKENS_PER_CELL];
	memset(tokens, 0, sizeof(struct Token) * MAX_TOKENS_PER_CELL);

	struct Lexer *lex = &sheet->lexer;
	lex->numline = 1;

	struct Cell  *thsc = &sheet->grid[0];
	struct Token *thsv = &tokens[0];

	while (lex->at < lex->t_bytes) {
		if (thsc->expr_len == MAX_TOKENS_PER_CELL)
			error_at_lexer(lex->src + lex->at, "maximum number of tokens reached (%d)", lex->numline, lex->linepos, MAX_TOKENS_PER_CELL);

		thsv->type = que_es_esto(lex);

		if (thsv->type == ttype_is_unknonw)
			error_at_lexer(lex->src + lex->at - 1, "unknown token", lex->numline, lex->linepos);
		if (thsv->type == ttype_is_space)
			continue;

		if (thsv->type == ttype_is_newline) {
			const size_t adv = sheet->cols * (sheet->lexer.numline - 1);
			thsc = &sheet->grid[adv];
			continue;
		}

		if (thsv->type == ttype_next_cell) {
			solve_cell(sheet, thsc, tokens);
			thsc++;
			thsv = &tokens[0];
			continue;
		}

		if (thsv->type == ttype_text)
			get_token_as_a_string(&thsv->as, lex);
		else if (thsv->type == ttype_is_number)
			get_token_as_a_number(&thsv->as.number, lex);
		else if (thsv->type == ttype_reference)
			get_token_as_a_reference(&thsv->as, lex);

		thsc->expr_len++;
		thsv++;
	}
}

static enum TokenType que_es_esto (struct Lexer *const lex)
{
	const char a = lex->src[lex->at++];
	lex->linepos++;

	if (isspace(a)) {
		if (a == 10) {
			lex->numline++;
			lex->linepos = 0;
			return ttype_is_newline;
		}
		return ttype_is_space;
	}

	switch (a) {
		case '|': case '"': case '@': case '^':
		case '<': case '>': case 'v': case '=':
		case '+': case '*': case '/': return a;
	}

	const char b = lex->src[lex->at];

	if (a == '-') {
		const char c = ((lex->at + 1) < lex->t_bytes) ? lex->src[lex->at + 1] : 0;

		if ((b == '0') && (c == 'x'))
			return ttype_is_number;

		return isdigit(b) ?  ttype_is_number : ttype_sub_sign;
	}

	if ((a == '0') && (b == 'x'))
		return ttype_is_number;

	return isdigit(a) ? ttype_is_number : ttype_is_unknonw;
}

static void get_token_as_a_string (union Value *const str, struct Lexer *const lex)
{
	str->text.len = 0;

	const unsigned starts_at = lex->linepos;
	str->text.src = lex->src + lex->at;

	while (lex->src[lex->at++] != '"') {
		if (str->text.len == MAX_TEXT_LENGTH)
			error_at_lexer(str->text.src, "text overflow, max length is %d", lex->numline, starts_at, MAX_TEXT_LENGTH);
		if (lex->src[lex->at] == '\n')
			error_at_lexer(str->text.src, "multiline string not allowed", lex->numline, starts_at);

		lex->linepos++;
		str->text.len++;
	}

	lex->linepos++;
}

static void get_token_as_a_number (long double *const num, struct Lexer *const lex)
{
	char *ends;
	char *from = lex->src + lex->at - 1;
	long double number = strtold(from, &ends);

	if (errno || (number >= LLONG_MAX))
		error_at_lexer(from, "number overflow", lex->numline, lex->linepos);

	const size_t diff = ends - from - 1;
	lex->linepos += diff;
	lex->at      += diff;

	*num = number;
}

static void get_token_as_a_reference (union Value *const ref, struct Lexer *const lex)
{
	size_t adv = 0;
	char *c	   = lex->src + lex->at;

	if (!isalpha(*c))
		goto malformed;

	unsigned short depth = 0;
	while (isalpha(*c)) {
		const char col = tolower(*c++);
		ref->reference.col += (depth++ * 26) + (col - 'a');
		adv++;
	}

	if (!isdigit(*c))
		goto malformed;

	char *ends;
	ref->reference.row = (unsigned short) strtoul(c, &ends, 10);

	adv += ends - c - 1;
	lex->linepos += adv;
	lex->at      += adv;

	return;
	malformed:
	error_at_lexer(c - adv - 1, "malformed reference", lex->numline, lex->linepos);
}

static void solve_cell (struct SheetInfo *const sheet, struct Cell *const cell, struct Token *const expression)
{
	/* Should reference by a pair of numbers which marks the row
	 * and column of the cell to be copied, or should it be the cell
	 * itself.
	 * */

	if (cell->expr_len == 0) {
		set_error_on_cell(cell, ctype_error_empty);
		return;
	}

	struct Token head = expression[0];

	switch (head.type) {
		case ttype_is_number:
			cell->as.number = head.as.number;
			cell->type = ctype_number;
			break;

		case ttype_text:
			cell->as.text.src = head.as.text.src;
			cell->as.text.len = head.as.text.len;
			cell->type = ctype_text;
			break;

		case ttype_reference:
			break;

		case ttype_expression:
		case ttype_clone_up:
		case ttype_clone_down:
		case ttype_clone_left:
		case ttype_clone_right:

		default:
			set_error_on_cell(cell, ctype_error_nosense);
			return;
	}
}

static void set_error_on_cell (struct Cell *const cell, const enum CellType wh)
{
	static const struct ErrCell errors[] = {
		{"![empty]",      8},
		{"![non-sense]", 12}
	};

	cell->as.text.src = errors[wh].error;
	cell->as.text.len = errors[wh].length;
	cell->type = wh;
}

static void print_outsheet (const struct SheetInfo *const sheet)
{
	struct Cell *cell;
	FILE *outf = stdout;

	for (unsigned short row = 0; row < sheet->rows; row++) {
		for (unsigned short col = 0; col < sheet->cols; col++) {
			cell = &sheet->grid[row * sheet->cols + col];

			if (cell->type == ctype_number) {
				fprintf(outf, "%Lf |", cell->as.number);
				continue;
			}
			if (cell->type != ctype_error_empty)
				fprintf(outf, "%.*s |", cell->as.text.len, cell->as.text.src);
			else
				fprintf(outf, " |");
		}
		fputc('\n', outf);
	}

	fclose(outf);
}
