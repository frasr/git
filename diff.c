#include "submodule.h"
	GIT_COLOR_NORMAL,	/* FUNCINFO */
	if (!strcasecmp(var+ofs, "func"))
		return DIFF_FUNCINFO;
	return -1;
		if (slot < 0)
			return 0;
typedef unsigned long (*sane_truncate_fn)(char *line, unsigned long len);

struct emit_callback {
	int color_diff;
	unsigned ws_rule;
	int blank_at_eof_in_preimage;
	int blank_at_eof_in_postimage;
	int lno_in_preimage;
	int lno_in_postimage;
	sane_truncate_fn truncate;
	const char **label_path;
	struct diff_words_data *diff_words;
	int *found_changesp;
	FILE *file;
	struct strbuf *header;
};

static int count_lines(const char *data, int size)
{
	int count, ch, completely_empty = 1, nl_just_seen = 0;
	count = 0;
	while (0 < size--) {
		ch = *data++;
		if (ch == '\n') {
			count++;
			nl_just_seen = 1;
			completely_empty = 0;
		}
		else {
			nl_just_seen = 0;
			completely_empty = 0;
		}
	}
	if (completely_empty)
		return 0;
	if (!nl_just_seen)
		count++; /* no trailing newline */
	return count;
}

static int fill_mmfile(mmfile_t *mf, struct diff_filespec *one)
{
	if (!DIFF_FILE_VALID(one)) {
		mf->ptr = (char *)""; /* does not matter */
		mf->size = 0;
		return 0;
	}
	else if (diff_populate_filespec(one, 0))
		return -1;

	mf->ptr = one->data;
	mf->size = one->size;
	return 0;
}

static int count_trailing_blank(mmfile_t *mf, unsigned ws_rule)
{
	char *ptr = mf->ptr;
	long size = mf->size;
	int cnt = 0;

	if (!size)
		return cnt;
	ptr += size - 1; /* pointing at the very end */
	if (*ptr != '\n')
		; /* incomplete line */
	else
		ptr--; /* skip the last LF */
	while (mf->ptr < ptr) {
		char *prev_eol;
		for (prev_eol = ptr; mf->ptr <= prev_eol; prev_eol--)
			if (*prev_eol == '\n')
				break;
		if (!ws_blank_line(prev_eol + 1, ptr - prev_eol, ws_rule))
			break;
		cnt++;
		ptr = prev_eol - 1;
	}
	return cnt;
}

static void check_blank_at_eof(mmfile_t *mf1, mmfile_t *mf2,
			       struct emit_callback *ecbdata)
{
	int l1, l2, at;
	unsigned ws_rule = ecbdata->ws_rule;
	l1 = count_trailing_blank(mf1, ws_rule);
	l2 = count_trailing_blank(mf2, ws_rule);
	if (l2 <= l1) {
		ecbdata->blank_at_eof_in_preimage = 0;
		ecbdata->blank_at_eof_in_postimage = 0;
		return;
	}
	at = count_lines(mf1->ptr, mf1->size);
	ecbdata->blank_at_eof_in_preimage = (at - l1) + 1;

	at = count_lines(mf2->ptr, mf2->size);
	ecbdata->blank_at_eof_in_postimage = (at - l2) + 1;
}

static void emit_line_0(FILE *file, const char *set, const char *reset,
			int first, const char *line, int len)
{
	int has_trailing_newline, has_trailing_carriage_return;
	int nofirst;

	if (len == 0) {
		has_trailing_newline = (first == '\n');
		has_trailing_carriage_return = (!has_trailing_newline &&
						(first == '\r'));
		nofirst = has_trailing_newline || has_trailing_carriage_return;
	} else {
		has_trailing_newline = (len > 0 && line[len-1] == '\n');
		if (has_trailing_newline)
			len--;
		has_trailing_carriage_return = (len > 0 && line[len-1] == '\r');
		if (has_trailing_carriage_return)
			len--;
		nofirst = 0;
	}

	if (len || !nofirst) {
		fputs(set, file);
		if (!nofirst)
			fputc(first, file);
		fwrite(line, len, 1, file);
		fputs(reset, file);
	}
	if (has_trailing_carriage_return)
		fputc('\r', file);
	if (has_trailing_newline)
		fputc('\n', file);
}

static void emit_line(FILE *file, const char *set, const char *reset,
		      const char *line, int len)
{
	emit_line_0(file, set, reset, line[0], line+1, len-1);
}

static int new_blank_line_at_eof(struct emit_callback *ecbdata, const char *line, int len)
{
	if (!((ecbdata->ws_rule & WS_BLANK_AT_EOF) &&
	      ecbdata->blank_at_eof_in_preimage &&
	      ecbdata->blank_at_eof_in_postimage &&
	      ecbdata->blank_at_eof_in_preimage <= ecbdata->lno_in_preimage &&
	      ecbdata->blank_at_eof_in_postimage <= ecbdata->lno_in_postimage))
		return 0;
	return ws_blank_line(line, len, ecbdata->ws_rule);
}

static void emit_add_line(const char *reset,
			  struct emit_callback *ecbdata,
			  const char *line, int len)
{
	const char *ws = diff_get_color(ecbdata->color_diff, DIFF_WHITESPACE);
	const char *set = diff_get_color(ecbdata->color_diff, DIFF_FILE_NEW);

	if (!*ws)
		emit_line_0(ecbdata->file, set, reset, '+', line, len);
	else if (new_blank_line_at_eof(ecbdata, line, len))
		/* Blank line at EOF - paint '+' as well */
		emit_line_0(ecbdata->file, ws, reset, '+', line, len);
	else {
		/* Emit just the prefix, then the rest. */
		emit_line_0(ecbdata->file, set, reset, '+', "", 0);
		ws_check_emit(line, len, ecbdata->ws_rule,
			      ecbdata->file, set, reset, ws);
	}
}

static void emit_hunk_header(struct emit_callback *ecbdata,
			     const char *line, int len)
{
	const char *plain = diff_get_color(ecbdata->color_diff, DIFF_PLAIN);
	const char *frag = diff_get_color(ecbdata->color_diff, DIFF_FRAGINFO);
	const char *func = diff_get_color(ecbdata->color_diff, DIFF_FUNCINFO);
	const char *reset = diff_get_color(ecbdata->color_diff, DIFF_RESET);
	static const char atat[2] = { '@', '@' };
	const char *cp, *ep;

	/*
	 * As a hunk header must begin with "@@ -<old>, +<new> @@",
	 * it always is at least 10 bytes long.
	 */
	if (len < 10 ||
	    memcmp(line, atat, 2) ||
	    !(ep = memmem(line + 2, len - 2, atat, 2))) {
		emit_line(ecbdata->file, plain, reset, line, len);
		return;
	}
	ep += 2; /* skip over @@ */

	/* The hunk header in fraginfo color */
	emit_line(ecbdata->file, frag, reset, line, ep - line);

	/* blank before the func header */
	for (cp = ep; ep - line < len; ep++)
		if (*ep != ' ' && *ep != '\t')
			break;
	if (ep != cp)
		emit_line(ecbdata->file, plain, reset, cp, ep - cp);

	if (ep < line + len)
		emit_line(ecbdata->file, func, reset, ep, line + len - ep);
}

static void emit_rewrite_lines(struct emit_callback *ecb,
			       int prefix, const char *data, int size)
	const char *endp = NULL;
	static const char *nneof = " No newline at end of file\n";
	const char *old = diff_get_color(ecb->color_diff, DIFF_FILE_OLD);
	const char *reset = diff_get_color(ecb->color_diff, DIFF_RESET);

	while (0 < size) {
		int len;

		endp = memchr(data, '\n', size);
		len = endp ? (endp - data + 1) : size;
		if (prefix != '+') {
			ecb->lno_in_preimage++;
			emit_line_0(ecb->file, old, reset, '-',
				    data, len);
		} else {
			ecb->lno_in_postimage++;
			emit_add_line(reset, ecb, data, len);
		size -= len;
		data += len;
	}
	if (!endp) {
		const char *plain = diff_get_color(ecb->color_diff,
						   DIFF_PLAIN);
		emit_line_0(ecb->file, plain, reset, '\\',
			    nneof, strlen(nneof));
	struct emit_callback ecbdata;
	memset(&ecbdata, 0, sizeof(ecbdata));
	ecbdata.color_diff = color_diff;
	ecbdata.found_changesp = &o->found_changes;
	ecbdata.ws_rule = whitespace_rule(name_b ? name_b : name_a);
	ecbdata.file = o->file;
	if (ecbdata.ws_rule & WS_BLANK_AT_EOF) {
		mmfile_t mf1, mf2;
		mf1.ptr = (char *)data_one;
		mf2.ptr = (char *)data_two;
		mf1.size = size_one;
		mf2.size = size_two;
		check_blank_at_eof(&mf1, &mf2, &ecbdata);
	}
	ecbdata.lno_in_preimage = 1;
	ecbdata.lno_in_postimage = 1;

		emit_rewrite_lines(&ecbdata, '-', data_one, size_one);
		emit_rewrite_lines(&ecbdata, '+', data_two, size_two);
/* In "color-words" mode, show word-diff of words accumulated in the buffer */
static void diff_words_flush(struct emit_callback *ecbdata)
{
	if (ecbdata->diff_words->minus.text.size ||
	    ecbdata->diff_words->plus.text.size)
		diff_words_show(ecbdata->diff_words);
}
		diff_words_flush(ecbdata);
static void find_lno(const char *line, struct emit_callback *ecbdata)
{
	const char *p;
	ecbdata->lno_in_preimage = 0;
	ecbdata->lno_in_postimage = 0;
	p = strchr(line, '-');
	if (!p)
		return; /* cannot happen */
	ecbdata->lno_in_preimage = strtol(p + 1, NULL, 10);
	p = strchr(p, '+');
	if (!p)
		return; /* cannot happen */
	ecbdata->lno_in_postimage = strtol(p + 1, NULL, 10);
}

	if (ecbdata->header) {
		fprintf(ecbdata->file, "%s", ecbdata->header->buf);
		strbuf_reset(ecbdata->header);
		ecbdata->header = NULL;
	}
	if (line[0] == '@') {
		if (ecbdata->diff_words)
			diff_words_flush(ecbdata);
		find_lno(line, ecbdata);
		emit_hunk_header(ecbdata, line, len);
	if (len < 1) {
		diff_words_flush(ecbdata);
	if (line[0] != '+') {
		const char *color =
			diff_get_color(ecbdata->color_diff,
				       line[0] == '-' ? DIFF_FILE_OLD : DIFF_PLAIN);
		ecbdata->lno_in_preimage++;
		if (line[0] == ' ')
			ecbdata->lno_in_postimage++;
		emit_line(ecbdata->file, color, reset, line, len);
	} else {
		ecbdata->lno_in_postimage++;
		emit_add_line(reset, ecbdata, line + 1, len - 1);
static void show_shortstats(struct diffstat_t *data, struct diff_options *options)
	struct strbuf header = STRBUF_INIT;

	if (DIFF_OPT_TST(o, SUBMODULE_LOG) &&
			(!one->mode || S_ISGITLINK(one->mode)) &&
			(!two->mode || S_ISGITLINK(two->mode))) {
		const char *del = diff_get_color_opt(o, DIFF_FILE_OLD);
		const char *add = diff_get_color_opt(o, DIFF_FILE_NEW);
		show_submodule_summary(o->file, one ? one->path : two->path,
				one->sha1, two->sha1,
				del, add, reset);
		return;
	}
	strbuf_addf(&header, "%sdiff --git %s %s%s\n", set, a_one, b_two, reset);
		strbuf_addf(&header, "%snew file mode %06o%s\n", set, two->mode, reset);
			strbuf_addf(&header, "%s%s%s\n", set, xfrm_msg, reset);
		strbuf_addf(&header, "%sdeleted file mode %06o%s\n", set, one->mode, reset);
			strbuf_addf(&header, "%s%s%s\n", set, xfrm_msg, reset);
			strbuf_addf(&header, "%sold mode %06o%s\n", set, one->mode, reset);
			strbuf_addf(&header, "%snew mode %06o%s\n", set, two->mode, reset);
			strbuf_addf(&header, "%s%s%s\n", set, xfrm_msg, reset);

			fprintf(o->file, "%s", header.buf);
			strbuf_reset(&header);
		fprintf(o->file, "%s", header.buf);
		strbuf_reset(&header);
		if (!DIFF_XDL_TST(o, WHITESPACE_FLAGS)) {
			fprintf(o->file, "%s", header.buf);
			strbuf_reset(&header);
		}

		if (ecbdata.ws_rule & WS_BLANK_AT_EOF)
			check_blank_at_eof(&mf1, &mf2, &ecbdata);
		ecbdata.header = header.len ? &header : NULL;
	strbuf_release(&header);
		if (data.ws_rule & WS_BLANK_AT_EOF) {
			struct emit_callback ecbdata;
			int blank_at_eof;

			ecbdata.ws_rule = data.ws_rule;
			check_blank_at_eof(&mf1, &mf2, &ecbdata);
			blank_at_eof = ecbdata.blank_at_eof_in_preimage;

			if (blank_at_eof) {
				static char *err;
				if (!err)
					err = whitespace_error_string(WS_BLANK_AT_EOF);
				fprintf(o->file, "%s:%d: %s.\n",
					data.filename, blank_at_eof, err);
				data.status = 1; /* report errors */
			}
	if ((ce->ce_flags & CE_VALID) || ce_skip_worktree(ce))
	retval = run_command_v_opt(spawn_arg, RUN_USING_SHELL);
	/*
	 * Most of the time we can say "there are changes"
	 * only by checking if there are changed paths, but
	 * --ignore-whitespace* options force us to look
	 * inside contents.
	 */

	if (DIFF_XDL_TST(options, IGNORE_WHITESPACE) ||
	    DIFF_XDL_TST(options, IGNORE_WHITESPACE_CHANGE) ||
	    DIFF_XDL_TST(options, IGNORE_WHITESPACE_AT_EOL))
		DIFF_OPT_SET(options, DIFF_FROM_CONTENTS);
	else
		DIFF_OPT_CLR(options, DIFF_FROM_CONTENTS);

	if (DIFF_OPT_TST(options, QUICK)) {
		DIFF_OPT_SET(options, QUICK);
	else if (!strcmp(arg, "--submodule"))
		DIFF_OPT_SET(options, SUBMODULE_LOG);
	else if (!prefixcmp(arg, "--submodule=")) {
		if (!strcmp(arg + 12, "log"))
			DIFF_OPT_SET(options, SUBMODULE_LOG);
	}
	for (;;) {

	/*
	 * Report the content-level differences with HAS_CHANGES;
	 * diff_addremove/diff_change does not set the bit when
	 * DIFF_FROM_CONTENTS is in effect (e.g. with -w).
	 */
	if (DIFF_OPT_TST(options, DIFF_FROM_CONTENTS)) {
		if (options->found_changes)
			DIFF_OPT_SET(options, HAS_CHANGES);
		else
			DIFF_OPT_CLR(options, HAS_CHANGES);
	}
	if (diff_queued_diff.nr && !DIFF_OPT_TST(options, DIFF_FROM_CONTENTS))
	if (!DIFF_OPT_TST(options, DIFF_FROM_CONTENTS))
		DIFF_OPT_SET(options, HAS_CHANGES);
	if (!DIFF_OPT_TST(options, DIFF_FROM_CONTENTS))
		DIFF_OPT_SET(options, HAS_CHANGES);
	child.use_shell = 1;
		close(child.out);
	close(child.out);