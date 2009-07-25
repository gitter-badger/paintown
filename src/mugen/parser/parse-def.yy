%{
#include <list>
#include <stdio.h>
#include <string.h>
#include <string>

#include "../ast/Configuration.h"
#include "../ast/Value.h"
#include "../ast/Modifier.h"
#include "util/system.h"
#include "globals.h"
#include "parse-exception.h"

#define bugon(a) if ((a)){ printf("parsing bug at %s:%d\n", __FILE__, __LINE__); }

extern "C" int yylex(void);
extern "C" int yyerror(const char *);

static Ast::Configuration *configuration;
static Ast::Section *currentSection;
static Ast::Variable *currentLhs;
static std::list<Ast::Value *> *currentRhs;
static Ast::Value *currentValue;
static std::list<Ast::Modifier *> *currentModifiers;

%}

%union {
    double numberValue;
    char *stringValue;
}
%token <stringValue> QUOTESTRING 
%token <numberValue> NUMBER 
%token <stringValue> IDENTIFIER
%token LBRACKET
%token RBRACKET

%token DEF_INFO
       DEF_DATA
       DEF_FILES
       DEF_ARCADE
       DEF_SCENEDEF
       DEF_SCENE
       DEF_BEGIN
       DEF_ACTION
       DEF_LOOPSTART
       DEF_HORIZONTAL
       DEF_VERTICAL
       DEF_VERTICAL_HORIZONTAL
       DEF_ALPHA_BLEND
       DEF_COLOR_ADDITION
       DEF_COLOR_SUBTRACT
       DEF_CAMERA
       DEF_PLAYERINFO
       DEF_SCALING
       DEF_REFLECTION
       DEF_BOUND
       DEF_STAGEINFO
       DEF_SHADOW
       DEF_MUSIC
       DEF_BGDEF
%token <stringValue> DEF_BG
%token DEF_BGCTRLDEF
%token <stringValue> DEF_BGCTRL
       

%token COMMENT
%token LINE_END

%error-verbose
%%
file: 
    end_or_comment file
    | stuff
    |
    ;

stuff:
    line ends stuff
    | line ends
    | line

ends:
    end_or_comment ends
    | end_or_comment

line:
    info
    | files
    | data
    | arcade
    | scene
    | camera
    | playerinfo
    | scaling
    | reflection
    | bound
    | stageinfo
    | shadow
    | music
    | bgdef
    | bg
    | bgctrldef
    | bgctrl
    | action
    | NUMBER ',' NUMBER ',' NUMBER ',' NUMBER ',' NUMBER maybe_flip
    | DEF_LOOPSTART
    | others
    | assignment
    | assign_none
    ;

assignment:
    lhs '=' rhs

lhs:
    variable
    | variable '(' value ')'

rhs:
   expression_list
   
assign_none:
    lhs '='

expression:
    variable '=' value
    | expression1

expression1:
    '(' expression ')'
    | '!' value
    | value
    | multiple_values

expression_list:
    expression
    | expression ',' expression_list

multiple_values:
    value
    | value multiple_values

value:
    NUMBER
    | QUOTESTRING
    | variable '(' expression_list ')'
    | variable
    ;

variable:
     IDENTIFIER '.' variable
     | IDENTIFIER

end_or_comment:
    LINE_END 
  | COMMENT
  ;

info:
   LBRACKET DEF_INFO RBRACKET;
   
files:
   LBRACKET DEF_FILES RBRACKET;
   
data:
   LBRACKET DEF_DATA RBRACKET;
   
arcade:
   LBRACKET DEF_ARCADE RBRACKET;
   
scene:
   LBRACKET DEF_SCENEDEF RBRACKET
   | scene_num
   ;
   
scene_num:
    LBRACKET DEF_SCENE NUMBER RBRACKET {
	double value = $3;
	Global::debug(0) << "Read Scene number " << value << std::endl;
    };
   
action:
    LBRACKET DEF_BEGIN DEF_ACTION NUMBER RBRACKET { 
	double value = $4;
	Global::debug(0) << "Read Action number " << value << std::endl;
	currentSection = new Ast::Section();
	// currentSection->setName($1);
	};

maybe_flip:
   | ',' flip
   | ',' flip ',' color_sub
   | ',' ',' color_sub
   |

flip:
    DEF_HORIZONTAL
    | DEF_VERTICAL
    | DEF_VERTICAL_HORIZONTAL
    | ','
    
color_sub:
    DEF_COLOR_ADDITION
    | DEF_COLOR_SUBTRACT
    | DEF_ALPHA_BLEND
    | ','
    
    
/* Implement properly later */
camera:
    LBRACKET DEF_CAMERA RBRACKET;
    
playerinfo:
    LBRACKET DEF_PLAYERINFO RBRACKET;

scaling:
    LBRACKET DEF_SCALING RBRACKET;

reflection:
    LBRACKET DEF_REFLECTION RBRACKET;

bound:
    LBRACKET DEF_BOUND RBRACKET;

stageinfo:
    LBRACKET DEF_STAGEINFO RBRACKET;

shadow:
    LBRACKET DEF_SHADOW RBRACKET;

music:
    LBRACKET DEF_MUSIC RBRACKET;

bgdef:
    LBRACKET DEF_BGDEF RBRACKET;

bg:
    DEF_BG {
	Global::debug(0) << "Got Bg: " << $1 << std::endl;
    };

bgctrldef:
    LBRACKET DEF_BGCTRLDEF bgctrldef_ident RBRACKET;

bgctrl:
    DEF_BGCTRL{
	Global::debug(0) << "Got BgCtrl: " << $1 << std::endl;
    };
    
bgctrldef_ident:
    IDENTIFIER{
	Global::debug(0) << "Got BgCtrlDef: " << $1 << std::endl;
	}
    | NUMBER{
	Global::debug(0) << "Got BgCtrlDef: " << $1 << std::endl;
    };

/* these are for quickly parsing system files, correct when needed */
others:
    LBRACKET IDENTIFIER RBRACKET
    | LBRACKET IDENTIFIER IDENTIFIER RBRACKET
    | LBRACKET IDENTIFIER NUMBER RBRACKET
    
%%

int yyerror(const char *msg) {
    extern int deflineno;
    extern char *deftext;
    printf("Parse error at line %d: %s at \n  \'%s\'\n", deflineno, msg, deftext);
    /*if (yytext)
	for (int i = 0; i < strlen(yytext); i++) {
	    printf("%d, ", yytext[i]);
	}*/
    return 0;
}

#include "parsers.h"

void Mugen::parseDef(const std::string & filename) throw (Mugen::ParserException) {
    extern FILE * defin;

    if (!System::readableFile(filename)){
    	throw ParserException(std::string("Cannot open ") + filename + " for reading");
    }

    defin = fopen(filename.c_str(), "r");
    if (defin == NULL){
    	throw ParserException(std::string("Could not open ") + filename);
    }
    int success = yyparse();
    fclose(defin);
    if (success == 0){
        Global::debug(0) << "Successfully parsed " << filename << std::endl;
    } else {
    	throw ParserException(std::string("Failed to parse ") + filename);
    }
}

#if 0
Ast::Configuration * mugenParse(std::string filename){
    /* lex input thing */
    extern FILE * yyin;

    /* todo: delete all this crap */
    if (configuration != NULL){
    	delete configuration;
    }

    configuration = new Ast::Configuration();
    currentSection = NULL;
    currentLhs = NULL;
    currentRhs = NULL;
    currentValue = NULL;
    currentModifiers = NULL;

    /* the lex reader thing */
    yyin = fopen(filename.c_str(), "r");
    yyparse();
    fclose(yyin);

    return configuration;
}
#endif

#undef bugon
