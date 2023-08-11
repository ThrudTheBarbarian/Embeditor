//
//  Editor.h
//  Embeditor
//
//  Created by Simon Gornall on 8/8/23.
//

#ifndef Editor_h
#define Editor_h

#include <cstdio>
#include <string>
#include <vector>

#include "properties.h"
#include "macros.h"

#define TERMIOS
#ifdef TERMIOS
#  include <termios.h>
#endif

class Editor
	{
    NON_COPYABLE_NOR_MOVEABLE(Editor)
    
	/*************************************************************************\
    |* Typedefs and enums
    \*************************************************************************/
    public:
		typedef std::vector<std::string> StringList;
		typedef std::function<void(std::string, int key)> promptCallback;
		
		/*********************************************************************\
		|* Syntax highlighting pattern control
		\*********************************************************************/
		typedef struct Syntax
			{
			std::string filetype;
			StringList filematch;
			StringList keywords;
			std::string singleLineCommentStart;
			std::string multiLineCommentStart;
			std::string multilineCommentEnd;
			int flags;
			} Syntax;

		/*********************************************************************\
		|* Special keys that we understand
		\*********************************************************************/
		typedef enum Key
			{
			BACKSPACE 		= 127,
			ARROW_LEFT 		= 1000,
			ARROW_RIGHT,
			ARROW_UP,
			ARROW_DOWN,
			DEL_KEY,
			HOME_KEY,
			END_KEY,
			PAGE_UP,
			PAGE_DOWN
			} Key;

		/*********************************************************************\
		|* Highlight-types
		\*********************************************************************/
		enum
			{
			HIGHLIGHT_NUMBERS = (1<<0),
			HIGHLIGHT_STRINGS = (1<<1)
			};
		
		typedef enum Highlight
			{
			HL_NORMAL = 0,
			HL_COMMENT,
			HL_MLCOMMENT,
			HL_KEYWORD1,
			HL_KEYWORD2,
			HL_STRING,
			HL_NUMBER,
			HL_MATCH
			} Highlight;

		typedef struct Row
			{
			int 					idx;
			int 					size;
			int 					rsize;
			std::string				chars;
			std::string				render;
			std::vector<uint8_t>	hl;
			int 					hl_open_comment;
			} Row;
		
		typedef std::vector<Row> RowList;
		
	/*************************************************************************\
    |* Properties
    \*************************************************************************/
    GET(int, cx);						// Current cursor X position
    GET(int, cy);						// Current cursor Y position
    GET(int, rx);						// Current render X position
    GET(int, rowOffset);				// Row offset in the file
    GET(int, colOffset);				// Column offset in the file
    GET(int, screenRows);				// Rows on the screen
    GET(int, screenCols);				// Columns on the screen
    GET(int, dirty);					// Have we made changes
    GET(std::string, filename);			// Path to the file
    GET(std::string, status);			// Status string at the bottom
    GET(time_t, statusTime);			// Cron for the status string
    GET(Syntax*, syntax);				// Highlighting syntax control
    GET(RowList, rows);					// List of rows of text
    GETSET(int, tabStop, TapStop);		// Tab stop value
        
    public:
        /*********************************************************************\
        |* Constructors and Destructor
        \*********************************************************************/
        explicit Editor();

        /*********************************************************************\
        |* Open a file
        \*********************************************************************/
        void open(std::string filename);
 
        /*********************************************************************\
        |* Run the editor
        \*********************************************************************/
		void edit(void);
 
        /*********************************************************************\
        |* Set the status message
        \*********************************************************************/
		void setStatus(const char *fmt, ...);

    private:
        /*********************************************************************\
        |* Save a file
        \*********************************************************************/
        void _save(void);
 
        /*********************************************************************\
        |* Get the window size
        \*********************************************************************/
        bool _windowSize(int *rows, int *cols);

        /*********************************************************************\
        |* Get the cursor position
        \*********************************************************************/
        bool _cursorPosition(int *rows, int *cols);

        /*********************************************************************\
        |* Enable raw mode
        \*********************************************************************/
        void _enableRawMode(void);
		
        /*********************************************************************\
        |* Update a row
        \*********************************************************************/
        void _update(int idx);
		
        /*********************************************************************\
        |* Refresh the screen
        \*********************************************************************/
        void _refreshScreen(void);
		
        /*********************************************************************\
        |* Refresh the screen
        \*********************************************************************/
        void _drawRows(std::string& buf);
		void _drawStatusBar(std::string& buf);
		void _drawMessageBar(std::string& buf);

        /*********************************************************************\
        |* Figure out row, col offsets
        \*********************************************************************/
        void _scroll(void);

        /*********************************************************************\
        |* Colour map for different types of highlight
        \*********************************************************************/
		int _syntaxToColor(int hl);

        /*********************************************************************\
        |* Colour map for different types of highlight
        \*********************************************************************/
		void _updateSyntax(Row& row);
		void _selectSyntaxHighlight(void);
		
        /*********************************************************************\
        |* Process any key presses
        \*********************************************************************/
        void _processKeypress(void);
        int  _readKey(void);
		void _moveCursor(int key);
		
        /*********************************************************************\
        |* editor operations
        \*********************************************************************/
		void _insertChar(int c);
		void _insertNewLine(void);
		void _delChar(void);
		void _find(void);
		void _findAction(std::string query, int key);
		
        /*********************************************************************\
        |* row operations
        \*********************************************************************/
		int  _rowCxToRx(int rowId, int cx);
		int  _rowRxToCx(int rowId, int rx);
		void _rowDelChar(Row& row, int at);
		void _rowAppendString(Row& row, std::string s);
		void _rowInsertChar(Row& row, int at, int c);
		void _delRow(int at);
		void _insertRow(std::string, int at);
 
        /*********************************************************************\
        |* Prompt the user
        \*********************************************************************/
		std::string _prompt(std::string prompt, promptCallback cb);


	};

#endif /* Editor_h */
