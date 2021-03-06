/* radare - LGPL - Copyright 2009-2013 - pancake */

#include "r_core.h"

#define NPF 5
static int blocksize = 0;
static void r_core_visual_refresh (RCore *core);
static const char *printfmt[] = {
	"x", "pd $r",
	"f tmp;sr sp;pxw 64;dr=;s-;s tmp;f-tmp;pd $r",
	"pxw", "pc"
};
static int autoblocksize = 1;
static int obs = 0;

static void showcursor(RCore *core, int x) {
	if (core && core->vmode)
		r_cons_show_cursor (x);
}

// XXX: use core->print->cur_enabled instead of curset/cursor/ocursor
static int curset = 0, cursor = 0, ocursor=-1;
static int color = 1;
static int debug = 1;
static int zoom = 0;

static int marks_init = 0;
static ut64 marks[UT8_MAX+1];

static int r_core_visual_hud(RCore *core) {
	const char *f = R2_LIBDIR"/radare2/"R2_VERSION"/hud/main";
	char *homehud = r_str_home (R2_HOMEDIR"/hud");
	char *res = NULL;
	char *p = 0;

	showcursor (core, R_TRUE);
	if (homehud)
		res = r_cons_hud_file (homehud);
	if (!res) {
		if (r_file_exists (f))
			res = r_cons_hud_file (f);
		else r_cons_message ("Cannot find hud file");
	}
	r_cons_clear ();
	if (res) {
		p = strchr (res, '\t');
		core->printidx = 1;
		r_cons_printf ("%s\n", res);
		r_cons_flush ();
		if (p) r_core_cmd0 (core, p+1);
		free (res);
	}
	showcursor (core, R_FALSE);
	r_cons_flush ();
	return (int)(size_t)p;
}

static void r_core_visual_mark_seek(RCore *core, ut8 ch) {
	if (!marks_init) {
		int i;
		for (i=0;i<UT8_MAX;i++)
			marks[i] = 0;
		marks_init = 1;
	}
	if (marks[ch])
		r_core_seek (core, marks[ch], 1);
}

static void visual_help() {
	r_cons_clear00 ();
	r_cons_printf (
		"Visual mode help:\n"
	" ?        show this help or manpage in cursor mode\n"
	" _        enter the hud\n"
	" .        seek to program counter\n"
	" /        in cursor mode search in current block\n"
	" :cmd     run radare command\n"
	" ;[-]cmt  add/remove comment\n"
	" /*+-[]   change block size, [] = resize hex.cols\n"
	" >||<     seek aligned to block size\n"
	" i/a/A    (i)nsert hex, (a)ssemble code, visual (A)ssembler\n"
	" b/B      toggle breakpoint / automatic block size\n"
	" c/C      toggle (c)ursor and (C)olors\n"
	" d[f?]    define function, data, code, ..\n"
	" D        enter visual diff mode (set diff.from/to)\n"
	" e        edit eval configuration variables\n"
	" f/F      set/unset flag\n"
	" gG       go seek to begin and end of file (0-$s)\n"
	" hjkl     move around (or HJKL) (left-down-up-right)\n"
	" mK/'K    mark/go to Key (any key)\n"
	" M        walk the mounted filesystems\n"
	" n/N      seek next/prev function/flag/hit (scr.nkey)\n"
	" p/P      rotate print modes (hex, disasm, debug, words, buf)\n"
	" q        back to radare shell\n"
	" R        randomize color palette (ecr)\n"
	" sS       step / step over\n"
	" t        track flags (browse symbols, functions..)\n"
	" T        browse anal info and comments\n"
	" v        visual code analysis menu\n"
	" V/W      (V)iew graph using cmd.graph (agv?), open (W)ebUI\n"
	" uU       undo/redo seek\n"
	" x        show xrefs to seek between them\n"
	" yY       copy and paste selection\n"
	" z        toggle zoom mode\n"
	);
	r_cons_flush ();
	r_cons_any_key ();
	r_cons_clear00 ();
}

static void r_core_visual_mark(RCore *core, ut8 ch) {
	if (!marks_init) {
		int i;
		for (i=0; i<UT8_MAX; i++)
			marks[i] = 0;
		marks_init = 1;
	}
	marks[ch] = core->offset;
}

static void prompt_read (const char *p, char *buf, int buflen) {
	r_line_set_prompt (p);
	showcursor (NULL, R_TRUE);
	r_cons_fgets (buf, buflen, 0, NULL);
	showcursor (NULL, R_FALSE);
}

R_API void r_core_visual_prompt (RCore *core) {
	char buf[1024];
	ut64 oseek = core->offset;
#if __UNIX__
	r_line_set_prompt (Color_RESET":> ");
#else
	r_line_set_prompt (":> ");
#endif
	showcursor (core, R_TRUE);
	r_cons_fgets (buf, sizeof (buf), 0, NULL);
	r_line_hist_add (buf);
	r_core_cmd (core, buf, 0);
	r_cons_any_key ();
	r_cons_clear00 ();
	showcursor (core, R_FALSE);
	if (curset) r_core_seek (core, oseek, 1);
}

static int visual_nkey(RCore *core, int ch) {
	const char *cmd;
	ut64 oseek = UT64_MAX;
	if (ocursor == -1) {
		oseek = core->offset;
		r_core_seek (core, core->offset + cursor, 0);
	}

	switch (ch) {
	case R_CONS_KEY_F1:
		cmd = r_config_get (core->config, "key.f1");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
		else ch = '?';
		break;
	case R_CONS_KEY_F2:
		cmd = r_config_get (core->config, "key.f2");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
		break;
	case R_CONS_KEY_F3:
		cmd = r_config_get (core->config, "key.f3");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
		break;
	case R_CONS_KEY_F4:
		cmd = r_config_get (core->config, "key.f4");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
	case R_CONS_KEY_F5:
		cmd = r_config_get (core->config, "key.f5");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
	case R_CONS_KEY_F6:
		cmd = r_config_get (core->config, "key.f6");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
		break;
	case R_CONS_KEY_F7:
		cmd = r_config_get (core->config, "key.f7");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
		else ch = 's';
		break;
	case R_CONS_KEY_F8:
		cmd = r_config_get (core->config, "key.f8");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
		break;
	case R_CONS_KEY_F9:
		cmd = r_config_get (core->config, "key.f9");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
		else r_core_cmd0 (core, "dc");
		break;
	case R_CONS_KEY_F10:
		cmd = r_config_get (core->config, "key.f10");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
		break;
	case R_CONS_KEY_F11:
		cmd = r_config_get (core->config, "key.f11");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
		break;
	case R_CONS_KEY_F12:
		cmd = r_config_get (core->config, "key.f12");
		if (cmd && *cmd) ch = r_core_cmd0 (core, cmd);
		break;
	}
	if (oseek != UT64_MAX)
		r_core_seek (core, oseek, 0);
	return ch;
}

static void setcursor (RCore *core, int cur) {
	int flags = core->print->flags; // wtf
	curset = cur;
	if (curset) flags |= R_PRINT_FLAGS_CURSOR;
	else flags &= ~(R_PRINT_FLAGS_CURSOR);
	core->print->cur_enabled = cur;
	r_print_set_flags (core->print, flags);
	core->print->col = curset? 1: 0;
}

static void setdiff (RCore *core) {
	char from[64], to[64];
	prompt_read ("diff from: ", from, sizeof (from));
	r_config_set (core->config, "diff.from", from);
	prompt_read ("diff to: ", to, sizeof (to));
	r_config_set (core->config, "diff.to", to);
}

// TODO: integrate in '/' command with search.inblock ?
static void visual_search (RCore *core) {
	const ut8 *p;
	int len, d = cursor;
	char str[128], buf[258];

	r_line_set_prompt ("search byte/string in block: ");
	r_cons_fgets (str, sizeof (str), 0, NULL);
	len = r_hex_str2bin (str, (ut8*)buf);
	if (*str=='"') {
		char *e = strncpy (buf, str+1, sizeof (buf)-1);
		if (e) { --e; if (*e=='"') *e=0; }
		len = strlen (buf);
	} else
	if (len<1) {
		strncpy (buf, str, sizeof (buf)-1);
		len = strlen (str);
	}
	p = r_mem_mem (core->block+d, core->blocksize-d,
		(const ut8*)buf, len);
	if (p) {
		cursor = (int)(size_t)(p-core->block);
		if (len>1) {
			ocursor = cursor+len-1;
		} else ocursor = -1;
		showcursor (core, R_TRUE);
		eprintf ("FOUND IN %d\n", cursor);
		r_cons_any_key ();
	} else {
		eprintf ("Cannot find bytes\n");
		r_cons_any_key ();
		r_cons_clear00 ();
	}
}

R_API void r_core_visual_seek_animation (RCore *core, ut64 addr) {
#if 0
	int i, ns = 90000;
	const char *scmd = (addr > core->offset)? "so": "s-4";
	for (i=0;i<5;i++) {
		r_core_cmd0 (core, scmd);
		r_core_visual_refresh (core);
		r_cons_flush();
		r_sys_usleep (ns);
		ns -= 1000;
	}
	r_core_seek (core, addr, 1);
#else
	if (core->offset == addr)
		return;
	r_cons_gotoxy (1, 2);
	if (addr>core->offset) {
		r_cons_printf (".----.\n");
		r_cons_printf ("| \\/ |\n");
		r_cons_printf ("'----'\n");
	} else {
		r_cons_printf (".----.\n");
		r_cons_printf ("| /\\ |\n");
		r_cons_printf ("'----'\n");
	}
	r_cons_flush();
	r_sys_usleep(90000);
#endif
	r_core_seek (core, addr, 1);
}

static void setprintmode (RCore *core, int n) {
	RAnalOp op;
	if (n>0) {
		core->printidx = R_ABS ((core->printidx+1)%NPF);
	} else {
		if (core->printidx)
			core->printidx--;
		else core->printidx = NPF-1;
	}
	switch (core->printidx) {
	case 0:
		core->inc = 16;
		break;
	case 1:
	case 2:
		core->inc = r_asm_disassemble (core->assembler,
			&op, core->block, 32);
		break;
	}
}

#define OPDELTA 32
static int prevopsz (RCore *core, ut64 addr) {
	ut64 target = addr;
	ut64 base = target-OPDELTA;
	int len, ret, i;
	ut8 buf[OPDELTA*2];
	RAnalOp op;

	r_core_read_at (core, base, buf, sizeof (buf));
	for (i=0; i<sizeof (buf); i++) {
		ret = r_anal_op (core->anal, &op, base+i,
			buf+i, sizeof (buf)-i);
		if (!ret) continue;
		len = op.length;
		r_anal_op_fini (&op); // XXX
		if (len<1) continue;
		i += len-1;
		if (target == base+i+1)
			return len;
	}
	return 4;
}

R_API int r_core_visual_cmd(RCore *core, int ch) {
	RAsmOp op;
	char buf[4096];
	int i, ret, offscreen, cols = core->print->cols;
	ch = r_cons_arrow_to_hjkl (ch);
	ch = visual_nkey (core, ch);
	if (ch<2) return 1;

	// do we need hotkeys for data references? not only calls?
	if (ch>='0'&& ch<='9') {
		ut64 off = core->asmqjmps[ch-'0'];
		if (off != UT64_MAX) {
			int delta = R_ABS ((st64)off-(st64)core->offset);
			r_io_sundo_push (core->io, core->offset);
			if (curset && delta<100) {
				cursor = delta;
			} else {
				r_core_visual_seek_animation (core, off);
				//r_core_seek (core, off, 1);
			}
			r_core_block_read (core, 1);
		}
	} else
	switch (ch) {
	case 90: // shift+tab
		if (!strcmp (printfmt[0], "x"))
			printfmt[0] = "pxa";
		else printfmt[0] = "x";
		break;
	case 9: // tab
		{ // XXX: unify diff mode detection
		ut64 f = r_config_get_i (core->config, "diff.from");
		ut64 t = r_config_get_i (core->config, "diff.to");
		if (f == t && f == 0) {
			core->print->col = core->print->col==1? 2: 1;
		} else {
			ut64 delta = core->offset - f;
			r_core_seek (core, t+delta, 1);
			r_config_set_i (core->config, "diff.from", t);
			r_config_set_i (core->config, "diff.to", f);
		}
		}
		break;
	case 'a':
		if (core->file && !(core->file->rwx & 2)) {
			r_cons_printf ("\nFile has been opened in read-only mode. Use -w flag\n");
			r_cons_any_key ();
			return R_TRUE;
		}
		r_cons_printf ("Enter assembler opcodes separated with ';':\n");
		showcursor (core, R_TRUE);
		r_cons_flush ();
		r_cons_set_raw (R_FALSE);
		strcpy (buf, "wa ");
		r_line_set_prompt (":> ");
		if (r_cons_fgets (buf+3, 1000, 0, NULL) <0) buf[0]='\0';
		if (*buf) {
			if (curset) r_core_seek (core, core->offset + cursor, 0);
			r_core_cmd (core, buf, R_TRUE);
			if (curset) r_core_seek (core, core->offset - cursor, 1);
		}
		showcursor (core, R_FALSE);
		r_cons_set_raw (R_TRUE);
		break;
	case 'A':
		{ int oc = curset;
		ut64 off = curset? core->offset+cursor : core->offset;
		curset = 0;
		r_core_visual_asm (core, off);
		curset = oc;
		}
		break;
	case 'c':
		setcursor (core, curset?0:1);
		break;
	case 'C':
		color = color? 0: 1;
		r_config_set_i (core->config, "scr.color", color);
		break;
	case 'd':
		r_core_visual_define (core);
		break;
	case 'D':
		setdiff (core);
		break;
	case 'f':
		{
		int range, min, max;
		char name[256], *n;
		r_line_set_prompt ("flag name: ");
		if (r_cons_fgets (name, sizeof (name), 0, NULL) >=0 && *name) {
			n = r_str_chop (name);
			if (*name=='-') {
				if (*n) r_flag_unset (core->flags, n+1, NULL);
			} else {
				if (ocursor != -1) {
					min = R_MIN (cursor, ocursor);
					max = R_MAX (cursor, ocursor);
				} else {
					min = max = cursor;
				}
				range = max-min+1;
				if (range<1) range = 1;
				if (*n) r_flag_set (core->flags, n,
					core->offset + min, range, 1);
			}
		} }
		break;
	case 'F':
		r_flag_unset_i (core->flags, core->offset + cursor, NULL);
		break;
	case 'n':
		r_core_seek_next (core, r_config_get (core->config, "scr.nkey"));
		break;
	case 'N':
		r_core_seek_previous (core, r_config_get (core->config, "scr.nkey"));
		break;
	case 'i':
	case 'I':
		if (core->file && !(core->file->rwx & 2)) {
			r_cons_printf ("\nFile has been opened in read-only mode. Use -w flag\n");
			r_cons_any_key ();
			return R_TRUE;
		}
		showcursor (core, R_TRUE);
		r_cons_flush ();
		r_cons_set_raw (0);
		if (ch=='I') {
			strcpy (buf, "wow ");
			r_line_set_prompt ("insert hexpair block: ");
			if (r_cons_fgets (buf+4, sizeof (buf)-3, 0, NULL) <0)
				buf[0]='\0';
			char *p = strdup (buf);
			int cur = core->print->cur;
			if (cur>=core->blocksize)
				cur = core->print->cur-1;
			snprintf (buf, sizeof (buf), "%s @ $$0!%i", p,
				core->blocksize-cursor);
			r_core_cmd (core, buf, 0);
			free (p);
			break;
		}
		if (core->print->col==2) {
			strcpy (buf, "\"w ");
			r_line_set_prompt ("insert string: ");
			if (r_cons_fgets (buf+3, sizeof (buf)-4, 0, NULL) <0)
				buf[0]='\0';
			strcat (buf, "\"");
		} else {
			strcpy (buf, "wx ");
			r_line_set_prompt ("insert hex: ");
			if (r_cons_fgets (buf+3, sizeof (buf)-4, 0, NULL) <0)
				buf[0]='\0';
		}
		if (curset) r_core_seek (core, core->offset + cursor, 0);
		r_core_cmd (core, buf, 1);
		if (curset) r_core_seek (core, core->offset - cursor, 1);
		r_cons_set_raw (1);
		showcursor (core, R_FALSE);
		break;
	case 'R':
		r_core_cmd0 (core, "ecr");
		break;
	case 'e':
		r_core_visual_config (core);
		break;
	case 'E':
		r_core_visual_colors (core);
		break;
	case 'M':
		r_core_visual_mounts (core);
		break;
	case 't':
		r_core_visual_trackflags (core);
		break;
	case 'x':
		{
		int count = 0;
		RList *xrefs = NULL;
		RAnalRef *refi;
		RListIter *iter;
		RAnalFunction *fun;

		if ((xrefs = r_anal_xref_get (core->anal, core->offset))) {
r_cons_gotoxy (1,1);
			r_cons_printf ("[GOTO XREF]> \n");
			if (r_list_empty (xrefs)) {
				r_cons_printf ("\tNo XREF found at 0x%"PFMT64x"\n", core->offset);
				r_cons_any_key ();
				r_cons_clear00 ();
			} else {
				r_list_foreach (xrefs, iter, refi) {
					fun = r_anal_fcn_find (core->anal, refi->addr, R_ANAL_FCN_TYPE_NULL);
					r_cons_printf (" [%i] %s XREF 0x%08"PFMT64x" (%s)                      \n", count,
							refi->type==R_ANAL_REF_TYPE_CODE?"CODE (JMP)":
							refi->type==R_ANAL_REF_TYPE_CALL?"CODE (CALL)":"DATA", refi->at,
							fun?fun->name:"unk");
					if (++count > 9) break;
				}
			}
		} else xrefs = NULL;
		r_cons_flush ();
		ch = r_cons_readchar ();
		if (ch >= '0' && ch <= '9') {
			refi = r_list_get_n (xrefs, ch-0x30);
			if (refi) {
				sprintf (buf, "s 0x%"PFMT64x, refi->at);
				r_core_cmd (core, buf, 0);
			}
		}
		if (xrefs)
			r_list_free (xrefs);
		}
		break;
	case 'T':
		r_core_visual_comments (core);
		break;
	case 'W':
		r_core_cmd0 (core, "=H");
		break;
	case 'V':
		r_core_cmd0 (core, "agv $$");
		break;
	case 'v':
		r_core_visual_anal (core);
		break;
	case 'g':
		if (core->io->va) {
			ut64 offset = r_io_section_get_vaddr (core->io, 0);
			if (offset == -1)
				offset = 0;
			r_core_seek (core, offset, 1);
		} else r_core_seek (core, 0, 1);
		r_io_sundo_push (core->io, core->offset);
		break;
	case 'G':
		ret = 0;
		if (core->io->va) {
			ut64 offset = r_io_section_get_vaddr (core->io, 0);
			if (offset == UT64_MAX) {
				offset = core->file->size - core->blocksize;
				ret = r_core_seek (core, offset, 1);
			} else {
				offset += core->file->size - core->blocksize;
				ret = r_core_seek (core, offset, 1);
			}
		} else {
			ret = r_core_seek (core,
				core->file->size-core->blocksize, 1);
		}
		if (ret != -1)
			r_io_sundo_push (core->io, core->offset);
		break;
	case 'h':
		if (curset) {
			cursor--;
			ocursor=-1;
			if (cursor<0) {
				r_core_seek_delta (core, -cols);
				cursor ++;
			}
		} else r_core_seek_delta (core, -1);
		break;
	case 'H':
		if (curset) {
			if (ocursor==-1) ocursor = cursor;
			cursor--;
			if (cursor<0) {
				r_core_seek (core, core->offset-cols, 1);
				cursor += cols;
				ocursor += cols;
			}
		} else r_core_seek_delta (core, -2);
		break;
	case 'l':
		if (curset) {
			cursor++;
			ocursor=-1;
			{
				int offscreen = (core->cons->rows-3)*cols;
				if (cursor>=offscreen) {
					r_core_seek (core, core->offset+cols, 1);
					cursor-=cols;
				}
			}
		} else r_core_seek_delta (core, 1);
		break;
	case 'L':
		if (curset) {
			if (ocursor==-1) ocursor = cursor;
			cursor++;
			offscreen = (core->cons->rows-3)*cols;
			if (cursor>=offscreen) {
				r_core_seek (core, core->offset+cols, 1);
				cursor-=cols;
				ocursor-=cols;
			}
		} else r_core_seek_delta (core, 2);
		break;
	case 'j':
		if (curset) {
			if (core->printidx == 1 || core->printidx == 2)
				cols = r_asm_disassemble (core->assembler,
					&op, core->block+cursor, 32);
			if (cols<1) cols = 1;
			cursor += cols;
			ocursor = -1;
			offscreen = (core->cons->rows-3)*cols;
			if (cursor>=offscreen) {
				r_core_seek (core, core->offset+cols, 1);
				cursor-=cols;
			}
		} else {
			if (core->printidx == 1 || core->printidx == 2) {
				cols = core->inc;
				//cols = r_asm_disassemble (core->assembler,
				//	&op, core->block+cursor, 32);
				core->asmsteps[core->curasmstep].offset = core->offset+cols;
				core->asmsteps[core->curasmstep].cols = cols;
				if (core->curasmstep < R_CORE_ASMSTEPS-1)
					core->curasmstep++;
				else core->curasmstep = 0;
			}
			r_core_seek (core, core->offset+cols, 1);
		}
		break;
	case 'J':
		if (curset) {
			if (core->printidx == 1 || core->printidx == 2) {
				cols = r_asm_disassemble (core->assembler,
					&op, core->block+cursor, 32);
				if (cols<1) cols = 1;
			}
			if (ocursor==-1) ocursor = cursor;
			cursor += cols;
			offscreen = (core->cons->rows-3)*cols;
			if (cursor>=offscreen) {
				r_core_seek (core, core->offset+cols, 1);
				cursor-=cols;
				ocursor-=cols;
			}
		} else r_core_seek (core, core->offset+obs, 1);
		break;
	case 'k':
		if (curset) {
			if (core->printidx == 1 || core->printidx == 2)
				cols = prevopsz (core, core->offset+cursor);
			cursor -= cols;
			ocursor = -1;
			if (cursor<0) {
				if (core->offset>=cols) {
					r_core_seek (core, core->offset-cols, 1);
					cursor += cols;
				}
			}
		} else {
			if (core->printidx == 1 || core->printidx == 2) {
				cols = prevopsz (core, core->offset);
			}
			if (core->offset >= cols)
				r_core_seek (core, core->offset-cols, 1);
			else r_core_seek (core, 0, 1);
		}
		break;
	case 'K':
		if (curset) {
			if (ocursor==-1) ocursor=cursor;
			if (core->printidx == 1 || core->printidx == 2)
				cols = prevopsz (core, core->offset+cursor);
			cursor -= cols;
			if (cursor<0) {
				if (core->offset>=cols) {
					r_core_seek (core, core->offset-cols, 1);
					ocursor += cols;
					cursor += cols;
				} else {
					r_core_seek (core, 0, 1);
					cursor = 0;
				}
			}
		} else {
			ut64 at = (core->offset>obs)?core->offset-obs:0;
			r_core_seek (core, at, 1);
		}
		break;
	case '[':
		{
			int scrcols = r_config_get_i (core->config, "hex.cols");
			if (scrcols>2)
				r_config_set_i (core->config, "hex.cols", scrcols-2);
		}
		break;
	case ']':
		{
			int scrcols = r_config_get_i (core->config, "hex.cols");
			r_config_set_i (core->config, "hex.cols", scrcols+2);
		}
		break;
#if 0
	case 'I':
		r_core_cmd (core, "dsp", 0);
		r_core_cmd (core, ".dr*", 0);
		break;
#endif
	case 's':
		if (curset) {
			// dcu 0xaddr
			char xxx[128];
			snprintf (xxx, sizeof (xxx), "dcu 0x%08"PFMT64x, core->offset + cursor);
			r_core_cmd (core, xxx, 0);
			curset = 0;
		} else {
			r_core_cmd (core, "ds", 0);
			r_core_cmd (core, ".dr*", 0);
		}
		break;
	case 'S':
		if (curset) {
			r_core_cmd (core, "dcr", 0);
			curset = 0;
		} else {
			r_core_cmd (core, "dso", 0);
			r_core_cmd (core, ".dr*", 0);
		}
		break;
	case 'p':
		setprintmode (core, 1);
		break;
	case 'P':
		setprintmode (core, -1);
		break;
	case 'm':
		r_core_visual_mark (core, r_cons_readchar ());
		break;
	case '\'':
		r_core_visual_mark_seek (core, r_cons_readchar ());
		break;
	case 'y':
		if (ocursor==-1) r_core_yank (core, core->offset+cursor, 1);
		else r_core_yank (core, core->offset+((ocursor<cursor)?
			ocursor:cursor), R_ABS (cursor-ocursor)+1);
		break;
	case 'Y':
		if (!core->yank_buf) {
			r_cons_strcat ("Can't paste, clipboard is empty.\n");
			r_cons_flush ();
			r_cons_any_key ();
			r_cons_clear00 ();
		} else r_core_yank_paste (core, core->offset+cursor, 0);
		break;
	case '-':
		if (core->print->cur_enabled) {
			int cur = core->print->cur;
			if (cur>=core->blocksize)
				cur = core->print->cur-1;
			if (ocursor==-1) sprintf (buf, "wos 01 @ $$+%i!1",cursor);
			else sprintf (buf, "wos 01 @ $$+%i!%i", cursor<ocursor?
				cursor:ocursor, R_ABS (ocursor-cursor)+1);
			r_core_cmd (core, buf, 0);
		} else {
			if (!autoblocksize)
				r_core_block_size (core, core->blocksize-1);
		}
		break;
	case '+':
		if (core->print->cur_enabled) {
			int cur = core->print->cur;
			if (cur>=core->blocksize)
				cur = core->print->cur-1;
			if (ocursor==-1) sprintf (buf, "woa 01 @ $$+%i!1", cursor);
			else sprintf (buf, "woa 01 @ $$+%i!%i",
				cursor<ocursor? cursor: ocursor, R_ABS (ocursor-cursor)+1);
			r_core_cmd (core, buf, 0);
		} else {
			if (!autoblocksize)
				r_core_block_size (core, core->blocksize+1);
		}
		break;
	case '/':
		if (curset) {
			visual_search (core);
		} else {
			if (!autoblocksize)
				r_core_block_size (core, core->blocksize-cols);
		}
		break;
	case '*':
		if (!autoblocksize)
			r_core_block_size (core, core->blocksize+cols);
		break;
	case '>':
		r_core_seek_align (core, core->blocksize, 1);
		r_io_sundo_push (core->io, core->offset);
		break;
	case '<':
		r_core_seek_align (core, core->blocksize, -1);
		r_core_seek_align (core, core->blocksize, -1);
		r_io_sundo_push (core->io, core->offset);
		break;
	case '.':
		if (r_debug_reg_get (core->dbg, "pc") != 0) {
			r_core_cmd (core, "sr pc", 0);
		} else {
			r_core_cmd (core, "s entry0", 0);
		}
		break;
#if 0
	case 'n': r_core_seek_delta (core, core->blocksize); break;
	case 'N': r_core_seek_delta (core, 0-(int)core->blocksize); break;
#endif
	case ':':
		{
			ut64 addr = core->offset;
			ut64 bsze = core->blocksize;
			if (curset) {
				if (ocursor != -1) {
					r_core_block_size (core, cursor-ocursor);
					r_core_seek (core, core->offset + ocursor, 1);
				} else {
					r_core_seek (core, core->offset + cursor, 1);
				}
			}
			r_core_visual_prompt (core);
			if (curset) {
				r_core_seek (core, addr, 1);
				r_core_block_size (core, bsze);
			}
		}
		break;
	case '_':
		if (r_config_get_i (core->config, "hud.once"))
			r_core_visual_hud (core);
		else	while (r_core_visual_hud (core));
		break;
	case ';':
		r_cons_printf ("Enter a comment: ('-' to remove, '!' to use $EDITOR)\n");
		showcursor (core, R_TRUE);
		r_cons_flush ();
		r_cons_set_raw (R_FALSE);
		strcpy (buf, "CC ");
		r_line_set_prompt ("comment: ");
		i = strlen (buf);
		if (r_cons_fgets (buf+i, sizeof (buf)-i-1, 0, NULL) >1) {
			ut64 orig = core->offset;
			ut64 addr = core->offset;
			if (curset) {
				addr += cursor;
				r_core_seek (core, addr, 0);
			}
			switch (buf[i]) {
			case '-':
				strcpy (buf, "CC-");
				break;
			case '!':
				strcpy (buf, "CC!");
				break;
			}
			r_core_cmd (core, buf, 1);
			if (curset) r_core_seek (core, orig, 1);
		}
		r_cons_set_raw (R_TRUE);
		showcursor (core, R_FALSE);
		break;
	case 'b':
		{
 		ut64 addr = curset? core->offset + cursor : core->offset;
		RBreakpointItem *bp = r_bp_get (core->dbg->bp, addr);
		if (bp) {
			r_bp_del (core->dbg->bp, addr);
		} else {
			r_bp_add_sw (core->dbg->bp, addr, 1, R_BP_PROT_EXEC);
		}
		}
		break;
	case 'B':
		autoblocksize = !autoblocksize;
		if (autoblocksize)
			obs = core->blocksize;
		else r_core_block_size (core, obs);
		r_cons_clear ();
		break;
	case 'u':
		{
		ut64 off = r_io_sundo (core->io, core->offset);
		if (off != UT64_MAX)
			//r_core_seek (core, off, 1);
			r_core_visual_seek_animation (core, off);
		else eprintf ("Cannot undo\n");
		}
		break;
	case 'U':
		{
		ut64 off = r_io_sundo_redo (core->io);
		if (off != UT64_MAX)
			//r_core_seek (core, off, 1);
			r_core_visual_seek_animation (core, off);
		}
		break;
	case 'z':
		if (zoom && cursor) {
			ut64 from = r_config_get_i (core->config, "zoom.from");
			ut64 to = r_config_get_i (core->config, "zoom.to");
			r_core_seek (core, from + ((to-from)/core->blocksize)*cursor, 1);
		}
		zoom = !zoom;
		break;
	case '?':
		if (curset) {
			char *man = NULL;
			/* check for manpage */
			RAnalOp *op = r_core_anal_op (core, core->offset+cursor);
			if (op) {
				if (op->jump != UT64_MAX) {
					RFlagItem *item = r_flag_get_i (core->flags, op->jump);
					if (item) {
						const char *ptr = r_str_lchr (item->name, '.');
						if (ptr)
							man = strdup (ptr+1);
					}
				}
				r_anal_op_free (op);
			}
			if (man) {
				char *p = strstr (man, "INODE");
				if (p) *p = 0;
				r_cons_clear();
				r_cons_flush();
				r_sys_cmdf ("man %s", man);
				free (man);
				break;
			}
		} else {
			visual_help();
}
		break;
	case 0x1b:
	case 'q':
	case 'Q':
		setcursor (core, 0);
		return R_FALSE;
	}
	r_core_block_read (core, 0);
	return R_TRUE;
}

#define PIDX (R_ABS(core->printidx%NPF))
R_API void r_core_visual_title (RCore *core, int color) {
	const char *BEGIN = core->cons->pal.prompt;
	const char *filename;
	char pos[512], foo[512], bar[512];
	int scrcols;
	/* automatic block size */
	if (autoblocksize)
	switch (core->printidx) {
	case 0:
		scrcols = r_config_get_i (core->config, "hex.cols");
		r_core_block_size (core, core->cons->rows * scrcols);
		break;
	case 3: // XXX pw
		scrcols = r_config_get_i (core->config, "hex.cols");
		r_core_block_size (core, core->cons->rows * scrcols);
		break;
	case 4: // XXX pc
		r_core_block_size (core, core->cons->rows * 5);
		break;
	case 1: // pd
	case 2: // pd+dbg
		r_core_block_size (core, core->cons->rows * 5); // this is hacky
		break;
	}

	filename = (core->file && core->file->filename)? core->file->filename: "";
	{ /* get flag with delta */
		ut64 addr = core->offset + (curset? cursor: 0);
		RFlagItem *f = r_flag_get_at (core->flags, addr);
		if (f) {
			if (f->offset == addr || !f->offset)
				snprintf (pos, sizeof (pos), "@ %s", f->name);
			else snprintf (pos, sizeof (pos), "@ %s+%d # 0x%"PFMT64x,
				f->name, (int)(addr-f->offset), addr);
		} else pos[0] = 0;
	}

	if (cursor<0) cursor = 0;

	if (color) r_cons_strcat (BEGIN);
	strncpy (bar, printfmt[PIDX], sizeof (bar)-1);

	bar[sizeof (bar)-1] = 0; // '\0'-terminate bar
	bar[10] = '.'; // chop cmdfmt
	bar[11] = '.'; // chop cmdfmt
	bar[12] = 0; // chop cmdfmt
	if (curset)
		snprintf (foo, sizeof (foo), "[0x%08"PFMT64x" %d (0x%x:%d=%d)]> %s %s\n",
				core->offset, core->blocksize, 
				cursor, ocursor, ocursor==-1?1:R_ABS (cursor-ocursor)+1,
				bar, pos);
	else
		snprintf (foo, sizeof (foo), "[0x%08"PFMT64x" %d %s]> %s %s\n",
			core->offset, core->blocksize, filename, bar, pos);
	r_cons_printf (foo);
	if (color) r_cons_strcat (Color_RESET);
}

static void r_core_visual_refresh (RCore *core) {
	RCons *cons;
	const char *vi, *vcmd;
	if (!core) return;
	r_cons_get_size (NULL);
	r_print_set_cursor (core->print, curset, ocursor, cursor);
	cons = r_cons_singleton ();
	cons->blankline = R_TRUE;

	/* hack to blank last line. move prompt here? */
	r_cons_fill_line ();
	if (autoblocksize) {
		r_cons_gotoxy (0, 0);
		r_cons_flush ();
	} else r_cons_clear ();

	r_cons_print_clear ();

	vi = r_config_get (core->config, "cmd.cprompt");
	if (vi && *vi) {
		// XXX: slow
		cons->blankline = R_FALSE;
		r_cons_printf ("[cmd.cprompt=%s]\n", vi);
		r_core_cmd (core, vi, 0);
		r_cons_column (r_config_get_i (core->config, "scr.colpos"));
		r_core_visual_title (core, color);
		r_cons_flush ();
		vi = r_config_get (core->config, "cmd.vprompt");
		if (vi) r_core_cmd (core, vi, 0);
	} else {
		vi = r_config_get (core->config, "cmd.vprompt");
		if (vi) r_core_cmd (core, vi, 0);
		r_core_visual_title (core, color);
	}

	vcmd = r_config_get (core->config, "cmd.visual");
	if (vcmd && *vcmd) {
		r_core_cmd (core, vcmd, 0);
	} else {
		if (zoom) r_core_cmd0 (core, "pz");
		else r_core_cmd0 (core, printfmt[PIDX]);
	}
	blocksize = core->num->value? core->num->value : core->blocksize;

	/* this is why there's flickering */
	r_cons_visual_flush ();
	cons->blankline = R_TRUE;
}

R_API int r_core_visual(RCore *core, const char *input) {
	const char *cmdprompt, *teefile;
	ut64 scrseek;
	int flags, ch;

	obs = core->blocksize;
	//r_cons_set_cup (R_TRUE);

	core->vmode = R_FALSE;
	while (*input) {
		if (!r_core_visual_cmd (core, input[0]))
			return 0;
		input++;
	}
	core->vmode = R_TRUE;
	core->cons->event_data = core;
	core->cons->event_resize = \
		(RConsEvent)r_core_visual_refresh;
	r_cons_show_cursor (R_FALSE);

	// disable tee in cons
	teefile = r_cons_singleton ()->teefile;
	r_cons_singleton ()->teefile = "";

	core->print->flags |=  R_PRINT_FLAGS_ADDRMOD;
	do {
		flags = core->print->flags;
		color = r_config_get_i (core->config, "scr.color");
		if (color) flags |= R_PRINT_FLAGS_COLOR;
		debug = r_config_get_i (core->config, "cfg.debug");
		flags = R_PRINT_FLAGS_ADDRMOD | R_PRINT_FLAGS_HEADER;
		r_print_set_flags (core->print, core->print->flags);
		scrseek = r_num_math (core->num,
			r_config_get (core->config, "scr.seek"));
		if (scrseek != 0LL)
			r_core_seek (core, scrseek, 1);
		if (debug)
			r_core_cmd (core, ".dr*", 0);
		cmdprompt = r_config_get (core->config, "cmd.vprompt");
		if (cmdprompt && *cmdprompt)
			r_core_cmd (core, cmdprompt, 0);
		r_core_visual_refresh (core);
		ch = r_cons_readchar ();
	} while (r_core_visual_cmd (core, ch));

	if (color)
		r_cons_printf (Color_RESET);
	r_config_set_i (core->config, "scr.color", color);
	core->print->cur_enabled = R_FALSE;
	if (autoblocksize)
		r_core_block_size (core, obs);
	r_cons_singleton ()->teefile = teefile;
	r_cons_clear00 ();
	r_cons_set_cup (R_FALSE);
	core->vmode = R_FALSE;
	core->cons->event_resize = NULL;
	core->cons->event_data = NULL;
	r_cons_show_cursor (R_TRUE);
	return 0;
}
