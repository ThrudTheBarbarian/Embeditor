//
//  Editor.cc
//  Embeditor
//
//  Created by Simon Gornall on 8/8/23.
//

#include <algorithm>
#include <cstring>

#include <ctype.h>
#include <cstdio>
#include <stdarg.h>
#include <unistd.h>

#include "Editor.h"

#ifdef TERMIOS
static struct termios orig_termios;

static void die(const char *s)
	{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
	}

static void disableRawMode(void)
	{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("tcsetattr");
	}
	
#endif

/*****************************************************************************\
|* Highlighting patterns. Should probably put these into a set of files
|* in /usr/share at some point
\*****************************************************************************/
std::vector<std::string> C_HL_extensions =
	{
	".c", ".h", ".cpp", ".cc"
	};
	
std::vector<std::string> C_HL_keywords =
	{
	"switch", "if", "while", "for", "break", "continue", "return", "else",
	"struct", "union", "typedef", "static", "enum", "class", "case",
	"int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
	"void|"
	};

struct Editor::Syntax HLDB[] =
	{
		{
		"c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/",
		Editor::HIGHLIGHT_NUMBERS | Editor::HIGHLIGHT_STRINGS
		},
	};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))


#define WELCOME_FMT 		"Editor -- version %s"
#define EDIT_VERSION		"0.0.1"
#define EDIT_QUIT_TIMES		3
#define CTRL_KEY(k) 		((k) & 0x1f)

/*****************************************************************************\
|* Constructor
\*****************************************************************************/
Editor::Editor()
	   :_cx(0)
	   ,_cy(0)
	   ,_rx(0)
	   ,_rowOffset(0)
	   ,_colOffset(0)
	   ,_screenRows(25)
	   ,_screenCols(80)
	   ,_dirty(0)
	   ,_filename("")
	   ,_status("")
	   ,_statusTime(0)
	   ,_syntax(nullptr)
	   ,_tabStop(4)
	{}

/*****************************************************************************\
|* Open a file to edit
\*****************************************************************************/
void Editor::open(std::string filename)
	{
	_filename = filename;
	_selectSyntaxHighlight();

	#ifdef TERMIOS
		FILE *fp = fopen(filename.c_str(), "r");
		if (fp == nullptr)
			die("fopen()");
		
		char *line 		= nullptr;
		size_t lineCap	= 0;
		ssize_t lineLen;
		while ((lineLen = getline(&line, &lineCap, fp)) != -1)
			{
			while ((lineLen >0) &&
				   ((line[lineLen-1] == '\n') || (line[lineLen-1] == '\r')))
				{
				lineLen --;
				}
			_insertRow(std::string(line, lineLen), (int)_rows.size());
			}
		FREE(line);
		fclose(fp);
		_dirty = 0;
	#else
	#endif
	}
		
/*****************************************************************************\
|* Set the status message
\*****************************************************************************/
void Editor::setStatus(const char *fmt, ...)
	{
	char buf[256]; // Longest status msg allowed. Still huge
	
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, 255, fmt, ap);
	va_end(ap);
	
	_status 		= buf;
	_statusTime 	= time(NULL);
	}
	
/*****************************************************************************\
|* Edit stuff
\*****************************************************************************/
void Editor::edit(void)
	{
	_enableRawMode();
	_windowSize(&_screenRows, &_screenCols);
	
	setStatus("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
	
	for(;;)
		{
		_refreshScreen();
		_processKeypress();
		}
	}
	
#pragma mark - Private Methods


/*****************************************************************************\
|* Save a file
\*****************************************************************************/
void Editor::_save(void)
	{
	#ifdef TERMIOS
		if (_filename.length() == 0)
			{
			_filename = _prompt("Save as: %s (ESC to cancel)", nullptr);
			if (_filename.length() == 0)
				{
				setStatus("Save aborted");
				return;
				}
			_selectSyntaxHighlight();
			}

		FILE *fp = fopen(_filename.c_str(), "w");
		if (fp != nullptr)
			{
			int totalBytes = 0;
			for (Row& row : _rows)
				{
				int len = (int) row.chars.length();
				totalBytes += len;
				if (fwrite(row.chars.c_str(), 1, len, fp) != len)
					{
					setStatus("Can't save! I/O error: %s [%d bytes saved]",
							  strerror(errno), totalBytes);
					fclose(fp);
					return;
					}
				}
			_dirty = 0;
			fclose(fp);
			setStatus("%d bytes written to disk", totalBytes);
			}
		else
			{
			setStatus("Can't open file '%s' for write", _filename.c_str());
			}
	#else
	#endif
	}

/*****************************************************************************\
|* Prompt the user
\*****************************************************************************/
std::string Editor::_prompt(std::string prompt, Editor::promptCallback cb)
	{
	std::string buf = "";
	
	while (true)
		{
		setStatus(prompt.c_str(), buf.c_str());
		_refreshScreen();

		int c = _readKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
			{
			if (buf.length() != 0)
				buf.erase(buf.end()-1);
			}
		else if (c == '\x1b')
			{
			setStatus("");
			return "";
			}
		else if (c == '\r')
			{
			if (buf.length() != 0)
				{
				setStatus("");
				return buf;
				}
			}
		else if (!iscntrl(c) && c < 128)
			{
			char ch = (char)c;
			buf.append(&ch, 1);
			}
		
		if (cb != nullptr)
			cb(buf, c);
		}
	}
	
/*****************************************************************************\
|* Refresh the screen
\*****************************************************************************/
void Editor::_refreshScreen(void)
	{
	_scroll();

	std::string abuf = "";

	// Hide the cursor and home
	abuf.append("\x1b[?25l");
	abuf.append("\x1b[H");

	_drawRows(abuf);
	_drawStatusBar(abuf);
	_drawMessageBar(abuf);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (_cy - _rowOffset) + 1,
											  (_rx - _colOffset) + 1);
	abuf.append(buf);
	
	// Show the cursor again
	abuf.append("\x1b[?25h");

	write(STDOUT_FILENO, abuf.data(), abuf.length());
	}

/*****************************************************************************\
|* Draw rows
\*****************************************************************************/
void Editor::_drawRows(std::string& buf)
	{
	int numRows = (int) _rows.size();
	
	for (int y = 0; y < _screenRows; y++)
		{
		int filerow = y + _rowOffset;
		if (filerow >= numRows)
			{
			if ((numRows == 0) && (y == _screenRows / 3))
				{
				char welcome[80];
				int welcomeLen = snprintf(welcome,
										  sizeof(welcome),
										  WELCOME_FMT,
										  EDIT_VERSION);
				if (welcomeLen > _screenCols)
					welcomeLen = _screenCols;
				int padding = (_screenCols - welcomeLen) / 2;
				if (padding)
					{
					buf.append("~");
					padding--;
					}
				while (padding--)
					buf.append(" ");
				buf.append(welcome, welcomeLen);
				}
			else
				buf.append("~");
			}
		else
			{
			Row& row = _rows.at(filerow);
			int len = row.rsize - _colOffset;
			if (len < 0)
				len = 0;
			if (len > _screenCols)
				len = _screenCols;
      
			const char *c 		= row.render.c_str() + _colOffset;
			uint8_t *hl 		= row.hl.data() + _colOffset;
			int current_color	= -1;
      
			for (int j = 0; j < len; j++)
				{
				if (iscntrl(c[j]))
					{
					char sym = (c[j] <= 26) ? '@' + c[j] : '?';
					buf.append("\x1b[7m");
					buf.append(&sym, 1);
					buf.append("\x1b[m");
					
					if (current_color != -1)
						{
						char cbuf[16];
						int clen = snprintf(cbuf,
											sizeof(cbuf),
											"\x1b[%dm",
											current_color);
						buf.append(cbuf, clen);
						}
					}
				else if (hl[j] == HL_NORMAL)
					{
					if (current_color != -1)
						{
						buf.append("\x1b[39m");
						current_color = -1;
						}
					buf.append(&c[j], 1);
					}
				else
					{
					int color = _syntaxToColor(hl[j]);
					if (color != current_color)
						{
						current_color = color;
						char cbuf[16];
						int clen = snprintf(cbuf,
											sizeof(cbuf),
											"\x1b[%dm",
											color);
						buf.append(cbuf, clen);
						}
					buf.append(&c[j], 1);
					}
				}
			buf.append("\x1b[39m");
			}

    	buf.append("\x1b[K");
    	buf.append("\r\n");
		}
	}

/*****************************************************************************\
|* Draw the status bar
\*****************************************************************************/
void Editor::_drawStatusBar(std::string& buf)
	{
  	buf.append("\x1b[7m");
	int numrows = (int) _rows.size();
	
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
		(_filename.length() > 0) ? _filename.c_str()
								 : "[No Name]",
		numrows,
		_dirty ? "(modified)" : "");
  
	int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
		(_syntax != nullptr) ? _syntax->filetype.c_str() : "no ft",
		_cy + 1, numrows);
		
	if (len > _screenCols)
		len = _screenCols;
  
	buf.append(status);
	while (len < _screenCols)
		{
		if (_screenCols - len == rlen)
			{
			buf.append(rstatus);
			break;
			}
		else
			{
			buf.append(" ");
			len++;
			}
		}
		
  	buf.append("\x1b[m");
	buf.append("\r\n");
	}

/*****************************************************************************\
|* Draw the message bar
\*****************************************************************************/
void Editor::_drawMessageBar(std::string& buf)
	{
  	buf.append("\x1b[K");
  
	int msglen = (int) _status.length();
	if (msglen > _screenCols)
		msglen = _screenCols;
		
	if (msglen && time(NULL) - _statusTime < 5)
    buf.append(_status);
	}

/*****************************************************************************\
|* Colour map for different types of highlight
\*****************************************************************************/
int Editor::_syntaxToColor(int hl)
	{
	switch (hl)
		{
		case HL_COMMENT:
		case HL_MLCOMMENT:
			return 36;
		case HL_KEYWORD1:
			return 33;
		case HL_KEYWORD2:
			return 32;
		case HL_STRING:
			return 35;
		case HL_NUMBER:
			return 31;
		case HL_MATCH:
			return 34;
		default:
			return 37;
		}
	}

/*****************************************************************************\
|* Figure out which colour-syntax-highlighting to use
\*****************************************************************************/
void Editor::_selectSyntaxHighlight(void)
	{
	_syntax = nullptr;
	if (_filename.length() == 0)
		return;

	std::size_t pos = _filename.rfind(".");
	if (pos != std::string::npos)
		{
		std::string ext = _filename.substr(pos);
		
		for (unsigned int j = 0; j < HLDB_ENTRIES; j++)
			{
			struct Editor::Syntax *s = &HLDB[j];
			
			for (std::string& match : s->filematch)
				{
				bool isExt 		= (match[0] == '.');
				bool matchExt	= isExt && (ext.length() > 0) && (ext == match);
				bool matchFile	= (!isExt) && (_filename == match);
				if (matchExt || matchFile)
					{
					_syntax = s;
					int numRows = (int) _rows.size();
					
					for (int i = 0; i < numRows; i++)
						_updateSyntax(_rows.at(i));
					return;
					}
				}
			}
		}
	}
	
/*****************************************************************************\
|* Figure out row, col offsets
\*****************************************************************************/
void Editor::_scroll(void)
	{
  	_rx = 0;
	if (_cy < _rows.size())
		_rx = _rowCxToRx(_cy, _cx);
  

	if (_cy < _rowOffset)
		_rowOffset = _cy;
  
	if (_cy >= _rowOffset + _screenRows)
		_rowOffset = _cy - _screenRows + 1;
  
	if (_rx < _colOffset)
		_colOffset = _rx;
  
	if (_rx >= _colOffset + _screenCols)
		_colOffset = _rx - _screenCols + 1;
	}
	
	

/*****************************************************************************\
|* Which chars are separators
\*****************************************************************************/
static int isSeparator(int c)
	{
	return isspace(c)
		|| (c == '\0')
		|| (strchr(",.()+-/*=~%<>[];", c) != NULL);
	}

/*****************************************************************************\
|* Update the syntax mappings within a row
\*****************************************************************************/
void Editor::_updateSyntax(Row& row)
	{
	row.hl.resize(row.rsize);
	memset(row.hl.data(), HL_NORMAL, row.rsize);

	if (_syntax == nullptr)
		return;

	StringList keywords = _syntax->keywords;
	std::string scs 	= _syntax->singleLineCommentStart;
	std::string mcs 	= _syntax->multiLineCommentStart;
  	std::string mce 	= _syntax->multilineCommentEnd;

	int scsLen 			= (int) scs.length();
	int mcsLen 			= (int) mcs.length();
	int mceLen 			= (int) mce.length();

	int prevSep 		= 1;
	int inString 		= 0;
	int inComment 		= (row.idx > 0 && _rows.at(row.idx-1).hl_open_comment);

	int i = 0;
	while (i < row.rsize)
		{
		char c 			= row.render.at(i);
    	uint8_t prev_hl = (i > 0) ? row.hl[i - 1] : HL_NORMAL;

		if (scsLen && !inString && !inComment)
			{
			if (row.render.substr(i, scsLen) == scs)
				{
				memset(row.hl.data()+i, HL_COMMENT, row.rsize - i);
				break;
				}
			}

		if (mcsLen && mceLen && !inString)
			{
			if (inComment)
				{
				*(row.hl.data() + i) = HL_MLCOMMENT;
				if (row.render.substr(i, mceLen) == mce)
					{
					memset(row.hl.data()+i, HL_MLCOMMENT, mceLen);
					i += mceLen;
					inComment = 0;
					prevSep = 1;
					continue;
					}
				else
					{
					i++;
					continue;
					}
				}
			else if (row.render.substr(i, mcsLen) == mcs)
				{
				memset(row.hl.data()+i, HL_MLCOMMENT, mcsLen);
				i += mcsLen;
				inComment = 1;
				continue;
				}
			}
		
		if (_syntax->flags & HIGHLIGHT_STRINGS)
			{
			if (inString)
				{
				*(row.hl.data()+i) = HL_STRING;
				if ((c == '\\') && (i + 1 < row.rsize))
					{
					*(row.hl.data() + i + 1) = HL_STRING;
					i += 2;
					continue;
					}
					
				if (c == inString)
					inString = 0;
				
				i++;
				prevSep = 1;
				continue;
				}
			else
				{
				if (c == '"' || c == '\'')
					{
					inString = c;
					row.hl[i] = HL_STRING;
					i++;
					continue;
					}
				}
			}

		if (_syntax->flags & HIGHLIGHT_NUMBERS)
			{
			bool prevNum = prevSep || (prev_hl == HL_NUMBER);
			bool prevHl  = (c == '.') && (prev_hl == HL_NUMBER);
			if ((isdigit(c) && prevNum) || prevHl)
				{
				*(row.hl.data()+i) = HL_NUMBER;
				i++;
				prevSep = 0;
				continue;
				}
			}

		if (prevSep)
			{
			int numKeywords = (int) keywords.size();
			int j = 0;
			for (j = 0; j<numKeywords; j++)
				{
				int klen 	= (int) keywords[j].length();
				bool kw2 	= keywords[j][klen - 1] == '|';
				if (kw2)
					klen--;
				
				std::string candidate = row.render.substr(i, klen);
				std::string match     = keywords[j].substr(0, klen);
				
				bool foundKW = (candidate == match);
				
				if (foundKW && isSeparator(row.render[i + klen]))
					{
					memset(row.hl.data() + i,
						   kw2 ? HL_KEYWORD2 : HL_KEYWORD1,
						   klen);
					i += klen;
					break;
					}
				}
			
			if (keywords[j] != "")
				{
				prevSep = 0;
				continue;
				}
			}

		prevSep = isSeparator(c);
		i++;
		}

	int changed = (row.hl_open_comment != inComment);
	row.hl_open_comment = inComment;
  
	if (changed && row.idx + 1 < _rows.size())
		_updateSyntax(_rows.at(row.idx+1));
	}
		
/*****************************************************************************\
|* Fetch the window size
\*****************************************************************************/
bool Editor::_windowSize(int *rows, int *cols)
	{
	bool ok = false;
	
	if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) == 12)
		ok = _cursorPosition(rows, cols);
	(*rows)-= 2;
	return ok;
	}

/*****************************************************************************\
|* Fetch the cursor position
\*****************************************************************************/
bool Editor::_cursorPosition(int *rows, int *cols)
	{
	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return false;

	while (i < sizeof(buf) - 1)
		{
		if (read(STDIN_FILENO, &(buf[i]), 1) != 1)
			return false;
		if (buf[i] == 'R')
			break;
		i++;
		}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[')
		return false;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return false;

	return true;
	}

/*****************************************************************************\
|* Enable raw mode
\*****************************************************************************/
void Editor::_enableRawMode(void)
	{
	#ifdef TERMIOS
		if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
			die("tcgetattr");

		atexit(disableRawMode);

		struct termios raw = orig_termios;
		raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
		raw.c_oflag &= ~(OPOST);
		raw.c_cflag |= (CS8);
		raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 1;

		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
			die("tcsetattr");
	#endif
	}

#pragma mark - User input

/*****************************************************************************\
|* Enable raw mode
\*****************************************************************************/
void Editor::_processKeypress(void)
	{
	static int quitTimes = EDIT_QUIT_TIMES;

	int c 			= _readKey();
	int numRows 	= (int) _rows.size();
	
	switch (c)
		{
		case '\r':
			_insertNewLine();
			break;

		case CTRL_KEY('q'):
			if (_dirty && quitTimes > 0)
				{
				setStatus("WARNING!!! File has unsaved changes. "
						  "Press Ctrl-Q %d more times to quit.",
						  quitTimes);
				quitTimes--;
				return;
				}
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;

		case CTRL_KEY('s'):
			_save();
			break;

		case HOME_KEY:
			_cx = 0;
			break;

		case END_KEY:
			if (_cy < numRows)
				_cx = _rows[_cy].size;
			break;

		case CTRL_KEY('f'):
			_find();
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			if (c == DEL_KEY)
				_moveCursor(ARROW_RIGHT);
			_delChar();
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
			if (c == PAGE_UP)
				{
				_cy = _rowOffset;
				}
			else if (c == PAGE_DOWN)
				{
				_cy = _rowOffset + _screenRows - 1;
				if (_cy > numRows)
					_cy = numRows;
				}

			int times = _screenRows;
			while (times--)
				_moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			break;
			}

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			_moveCursor(c);
			break;

		case CTRL_KEY('l'):
			case '\x1b':
			break;

		default:
			_insertChar(c);
			break;
		}

	quitTimes = EDIT_QUIT_TIMES;
	}

/*****************************************************************************\
|* Read a key from stdin
\*****************************************************************************/
int Editor::_readKey(void)
	{
	int nread;
	char c;
	while ((nread = (int)read(STDIN_FILENO, &c, 1)) != 1)
		{
		if (nread == -1 && errno != EAGAIN)
			die("read");
		}

	if (c == '\x1b')
		{
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';

		if (seq[0] == '[')
			{
			if (seq[1] >= '0' && seq[1] <= '9')
				{
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\x1b';
					
				if (seq[2] == '~')
					{
					switch (seq[1])
						{
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
						}
					}
				}
			else
				{
				switch (seq[1])
					{
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
					}
				}
			}
		else if (seq[0] == 'O')
			{
			switch (seq[1])
				{
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
				}
			}

		return '\x1b';
		}
	else
		return c;
	}


/*****************************************************************************\
|* Move the cursor
\*****************************************************************************/
void Editor::_moveCursor(int key)
	{
	int numRows 	= (int) _rows.size();
	bool validRow	= (_cy < numRows);

	switch (key)
		{
		case ARROW_LEFT:
			if (_cx != 0)
				_cx--;
			else if (_cy > 0)
				{
				_cy--;
				_cx = _rows.at(_cy).size;
				}
			break;
    
		case ARROW_RIGHT:
			if (validRow && (_cx < _rows.at(_cy).size))
				_cx++;
			else if (validRow && (_cx == _rows.at(_cy).size))
				{
				_cy++;
				_cx = 0;
				}
			break;
    
		case ARROW_UP:
			if (_cy != 0)
				_cy--;
			break;
    
		case ARROW_DOWN:
			if (_cy < numRows)
				_cy++;
			break;
		}

	numRows 	= (int) _rows.size();
	validRow	= (_cy < numRows);

	int rowlen = validRow ? _rows.at(_cy).size : 0;
	if (_cx > rowlen)
		_cx = rowlen;
	}

#pragma mark - Editor Operations

/*****************************************************************************\
|* Insert a character
\*****************************************************************************/
void Editor::_insertChar(int c)
	{
	int numRows = (int) _rows.size();
	if (_cy == numRows)
		_insertRow("", numRows);
  
  	_rowInsertChar(_rows.at(_cy), _cx, c);
	_cx++;
	}

/*****************************************************************************\
|* Insert a new line
\*****************************************************************************/
void Editor::_insertNewLine(void)
	{
	if (_cx == 0)
		_insertRow("", _cy);
	else
		{
    	Row& row = _rows.at(_cy);
    	_insertRow(row.chars.data()+ _cx, _cy + 1);
		row = _rows.at(_cy);
		row.size = _cx;
		row.chars.resize(row.size);
		_update(_cy);
		}
	_cy++;
	_cx = 0;
	}

/*****************************************************************************\
|* Delete a character
\*****************************************************************************/
void Editor::_delChar(void)
	{
	int numRows = (int) _rows.size();
	if (_cy == numRows)
		return;
	if ((_cx == 0) && (_cy == 0))
		return;

	Row& row = _rows.at(_cy);
	if (_cx > 0)
		{
		_rowDelChar(row, _cx - 1);
		_cx--;
		}
	else
		{
		_cx = _rows.at(_cy - 1).size;
		_rowAppendString(_rows.at(_cy - 1), row.chars);
		_delRow(_cy);
		_cy--;
		}
	}

/*****************************************************************************\
|* Find a string
\*****************************************************************************/
void Editor::_find(void)
	{
	int savedCx 		= _cx;
	int savedCy 		= _cy;
	int savedColOffset 	= _colOffset;
	int savedRowOffset 	= _rowOffset;

	std::string query = _prompt("Search: %s (Use ESC/Arrows/Enter)",
								std::bind(&Editor::_findAction,
										  this,
										  std::placeholders::_1,
										  std::placeholders::_2));

	if (query.length() == 0)
		{
		_cx 		= savedCx;
		_cy 		= savedCy;
		_colOffset	= savedColOffset;
		_rowOffset 	= savedRowOffset;
		}
	}

/*****************************************************************************\
|* Actually do the find
\*****************************************************************************/
void Editor::_findAction(std::string query, int key)
	{
	static int last_match 	= -1;
	static int direction  	= 1;
	static std::vector<uint8_t> savedHl;
	static int savedHlLine;

	if (savedHl.size() > 0)
		{
		_rows.at(savedHlLine).hl = savedHl;
		savedHl.clear();
		}

	if (key == '\r' || key == '\x1b')
		{
		last_match = -1;
		direction = 1;
		return;
		}
	else if (key == ARROW_RIGHT || key == ARROW_DOWN)
		{
		direction = 1;
		}
	else if (key == ARROW_LEFT || key == ARROW_UP)
		{
		direction = -1;
		}
	else
		{
		last_match = -1;
		direction = 1;
		}

	if (last_match == -1)
		direction = 1;
	int current = last_match;
  
	int numRows = (int)_rows.size();
	for (int i = 0; i < numRows; i++)
		{
		current += direction;
		if (current == -1)
			current = numRows - 1;
		else if (current == numRows)
			current = 0;

    	Row& row 	= _rows.at(current);
		const char *match = strstr(row.render.c_str(), query.c_str());
		if (match)
			{
			last_match = current;
			_cy = current;
			_cx = _rowRxToCx(row.idx, (int)(match - row.render.c_str()));
			_rowOffset = numRows;

			savedHlLine = current;
			savedHl		= row.hl;
			memset(&(row.hl[match - row.render.c_str()]),
					HL_MATCH,
					query.length());
			break;
			}
		}
	}

#pragma mark - Row operations
/*****************************************************************************\
|* Figure out the render x from the column x
\*****************************************************************************/
int Editor::_rowCxToRx(int rowId, int cx)
	{
	int rx = 0;
	Row& row = _rows.at(rowId);
	
	for (int j=0; j < cx; j++)
		{
		if (row.chars.at(j) == '\t')
			rx += (_tabStop - 1) - (rx % _tabStop);
		rx++;
		}
	return rx;
	}

/*****************************************************************************\
|* Figure out the column x from the render x
\*****************************************************************************/
int Editor::_rowRxToCx(int rowId, int rx)
	{
	int cur_rx = 0;
	int cx;
 	Row& row = _rows.at(rowId);
 
	for (cx = 0; cx < row.size; cx++)
		{
		if (row.chars.at(cx) == '\t')
			cur_rx += (_tabStop - 1) - (cur_rx % _tabStop);
		cur_rx++;

		if (cur_rx > rx)
			return cx;
		}
	return cx;
	}
/*****************************************************************************\
|* Update a row
\*****************************************************************************/
void Editor::_update(int rowIndex)
	{
	Row& row 	= _rows.at(rowIndex);
	row.render	= "";

	int idx 	= 0;
	for (int j = 0; j < row.size; j++)
		{
		if (row.chars.at(j) == '\t')
			{
			row.render.append(" ");
			idx ++;
			while (idx % _tabStop != 0)
				{
				row.render.append(" ");
				idx ++;
				}
			}
		else
			{
			row.render.append(row.chars, j, 1);
			idx ++;
			}
		}
  
	row.rsize = idx;

	_updateSyntax(row);
	}


/*****************************************************************************\
|* Insert a row
\*****************************************************************************/
void Editor::_insertRow(std::string s, int at)
	{
	if ((at >= 0) && (at <= _rows.size()))
		{
		Row row =
			{
			.idx 				= at,
			.size				= (int)s.length(),
			.chars  			= s,
			.rsize  			= 0,
			.render 			= "",
			.hl_open_comment	= 0
			};
		_rows.insert(_rows.begin()+at, row);
		_update(at);
		_dirty ++;
		}
	}

/*****************************************************************************\
|* Delete a row
\*****************************************************************************/
void Editor::_delRow(int at)
	{
	int numRows = (int) _rows.size();

	if (at < 0 || at >= numRows)
		return;
	_rows.erase(_rows.begin()+at);
	for (int j = at; j < numRows - 1; j++)
		_rows.at(j).idx--;
	_dirty++;
	}

/*****************************************************************************\
|* Insert a character in a row
\*****************************************************************************/
void Editor::_rowInsertChar(Row& row, int at, int c)
	{
	if ((at < 0) || (at > row.size))
		at = row.size;
		
	row.chars.insert(at, 1, c);

	row.size++;
  	_update(row.idx);
	_dirty++;
	}

/*****************************************************************************\
|* Append a string to a row
\*****************************************************************************/
void Editor::_rowAppendString(Row& row, std::string s)
	{
	row.chars.append(s);
	row.size += s.length();
  	_update(row.idx);
  	_dirty++;
	}

/*****************************************************************************\
|* Delete a character from a row
\*****************************************************************************/
void Editor::_rowDelChar(Row& row, int at)
	{
	if ((at < 0) || (at >= row.size))
		return;
	
	row.chars.erase(row.chars.begin()+at);
	row.size--;
	_update(row.idx);
	_dirty++;
	}
