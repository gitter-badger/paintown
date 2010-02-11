#include "util/bitmap.h"
#include "mugen/character-select.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <string>
#include <cstring>
#include <vector>
#include <ostream>
#include <sstream>
#include <iostream>

#include "mugen/stage.h"

#include "init.h"
#include "resource.h"
#include "util/funcs.h"
#include "util/file-system.h"
#include "util/timedifference.h"
#include "game/console.h"
#include "object/animation.h"
#include "object/object.h"
#include "object/character.h"
#include "object/object_attack.h"
#include "object/player.h"
#include "globals.h"
#include "factory/font_render.h"
#include "shutdown_exception.h"

#include "menu/menu_option.h"
#include "menu/menu_global.h"
#include "menu/option_quit.h"
#include "menu/option_dummy.h"

#include "mugen/animation.h"
#include "mugen/background.h"
#include "character.h"
#include "mugen/sound.h"
#include "mugen/reader.h"
#include "mugen/sprite.h"
#include "mugen/util.h"
#include "mugen/font.h"

#include "mugen/option-versus.h"
#include "ast/all.h"
#include "parser/all.h"

#include "loading.h"

#include "input/input-manager.h"

#include "return_exception.h"

namespace PaintownUtil = ::Util;

using namespace std;
using namespace Mugen;

static const int DEFAULT_WIDTH = 320;
static const int DEFAULT_HEIGHT = 240;
static const int DEFAULT_SCREEN_X_AXIS = 160;
static const int DEFAULT_SCREEN_Y_AXIS = 0;


static const std::string fixStageName(const std::string &stage){
    // *FIXME not a good solution to get file
    std::string ourDefFile = stage;
    std::string baseDir = Filesystem::find("mugen/stages/");
    if (ourDefFile.find(".def")==std::string::npos){
	ourDefFile+=".def";
    }
    // Get correct directory
    baseDir = Mugen::Util::getFileDir(baseDir + ourDefFile);
    ourDefFile = Mugen::Util::getCorrectFileLocation(baseDir, Mugen::Util::stripDir(ourDefFile));
    return ourDefFile;
}

FontHandler::FontHandler():
state(Normal),
font(0),
bank(0),
position(0),
blinkFont(0),
blinkBank(0),
blinkPosition(0),
doneFont(0),
doneBank(0),
donePosition(0),
ticker(0),
blinkTime(10),
blinkState(Normal){
}

FontHandler::~FontHandler(){
}

void FontHandler::act(){
    switch(state){
	case Blink:
	    ticker++;
	    if (ticker == blinkTime){
		ticker = 0;
		if (blinkState == Normal){
		    blinkState = Blink;
		} else if (blinkState == Blink){
		    blinkState = Normal;
		}
	    }
	    break;
	case Normal:
	case Done:
	default:
	    break;
    }
}

void FontHandler::render(const std::string &text, const Bitmap &bmp){
    switch(state){
	default:
	case Normal:
	    font->render(location.x, location.y, position, bank, bmp, text);
	    break;
	case Blink:
	    if (blinkState == Normal){
		font->render(location.x, location.y, position, bank, bmp, text);
	    } else if (blinkState == Blink){
		blinkFont->render(location.x, location.y, blinkPosition, blinkBank, bmp, text);
	    }
	    break;
	case Done:
	    doneFont->render(location.x, location.y, donePosition, doneBank, bmp, text);
	    break;
    }
}

CharacterInfo::CharacterInfo(const std::string &definitionFile):
definitionFile(definitionFile),
baseDirectory(Util::getFileDir(definitionFile)),
spriteFile(Util::probeDef(definitionFile,"files","sprite")),
name(Util::probeDef(definitionFile,"info","name")),
displayName(Util::probeDef(definitionFile,"info","displayname")),
currentAct(0),
icon(0),
portrait(0),
randomStage(false),
order(1),
referenceCell(0),
character1(0),
character2(0){
    /* Grab the act files, in mugen it's strictly capped at 12 so we'll do the same */
    for (int i = 0; i < 12; ++i){
        stringstream act;
        act << "pal" << i;
        try {
            std::string actFile = Util::probeDef(definitionFile,"files",act.str());
            actCollection.push_back(actFile);
        } catch (const MugenException &me){
            // Ran its course got what we needed
        }
    }
    // just a precaution
    spriteFile = Util::removeSpaces(spriteFile);
    icon = Util::probeSff(baseDirectory + spriteFile,9000,0,baseDirectory + actCollection[currentAct]);
    portrait = Util::probeSff(baseDirectory + spriteFile,9000,1,baseDirectory + actCollection[currentAct]);
}

CharacterInfo::~CharacterInfo(){
    if (icon){
        delete icon;
    }
    if (portrait){
        delete portrait;
    }
    if (character1){
	delete character1;
    }
    if (character2){
	delete character2;
    }
}

void CharacterInfo::loadPlayer1(){
    if (character1){
	return;
    }
    character1 = new Mugen::Character(definitionFile);
    character1->load();
}

void CharacterInfo::loadPlayer2(){
    if (character2){
	return;
    }
    character2 = new Mugen::Character(definitionFile);
    character2->load();
}

void CharacterInfo::setAct(int number){
    if (number == currentAct){
        return;
    }

    currentAct = number;
    if (icon){
        delete icon;
    }

    if (portrait){
        delete portrait;
    }

    icon = Util::probeSff(baseDirectory + spriteFile,9000,0,baseDirectory + actCollection[currentAct]);
    portrait = Util::probeSff(baseDirectory + spriteFile,9000,1,baseDirectory + actCollection[currentAct]);
}

// Stage selector
StageHandler::StageHandler():
currentStage(0),
display(false),
selecting(true),
moveSound(0),
selectSound(0){
    stages.push_back("Random");
    stageNames.push_back("Stage: Random");
}

StageHandler::~StageHandler(){
}

void StageHandler::act(){
    font.act();
}

void StageHandler::render(const Bitmap &bmp){
    if (display){
	font.render(stageNames[currentStage],bmp);
    }
}
	
//! Get current selected stage
const std::string &StageHandler::getStage(){
    // check if random first;
    if (currentStage == 0){
	return getRandomStage();
    }
    return stages[currentStage];
}

//! Get random stage
const std::string &StageHandler::getRandomStage(){
    return stages[PaintownUtil::rnd(1,stages.size())];
}

//! Set Next Stage
void StageHandler::next(){
    if (!selecting){
	return;
    }
    if (currentStage < stages.size()-1){
	currentStage++;
    } else {
	currentStage = 0;
    }
    if (stages.size() > 1){
        if (moveSound){
            moveSound->play();
        }
    }
}

//! Set Prev Stage
void StageHandler::prev(){
    if (!selecting){
	return;
    }
    if (currentStage > 0){
	currentStage--;
    } else {
	currentStage = stages.size()-1;
    }
    if (stages.size() > 1){
        if (moveSound){
            moveSound->play();
        }
    }
}

void StageHandler::toggleSelecting(){
    selecting = !selecting;
    if (!selecting) {
	font.setState(font.Done);
        if (selectSound){
            selectSound->play();
        }
    }
}

//! Add stage to list
void StageHandler::addStage(const std::string &stage){
    try {
	// *FIXME not a good solution to get file
	std::string ourDefFile = fixStageName(stage);
	stringstream temp;
        temp << "Stage " << stages.size() << ": " << Util::probeDef(ourDefFile,"info","name");
	stageNames.push_back(temp.str());
	stages.push_back(ourDefFile);
    } catch (const MugenException &ex){
	Global::debug(0) << "Problem adding stage. Reason: " << ex.getReason() << endl;
    }
}

// Cell
Cell::Cell(int x, int y):
location(x,y),
background(0),
randomSprite(0),
random(false),
empty(true),
characterScaleX(1),
characterScaleY(1),
flash(0),
cursors(None){
}

Cell::~Cell(){
}

void Cell::act(){
    if (flash){
	flash--;
    }
}

void Cell::randomize(std::vector<CharacterInfo *> &characters){
    if (random){
	unsigned int num = PaintownUtil::rnd(0,characters.size());
        character = characters[num];
    }
}

void Cell::render(const Bitmap & bmp){
    background->render(position.x,position.y,bmp);
    if (!empty){
	Mugen::Effects effects;
	effects.scalex = characterScaleX;
	effects.scaley = characterScaleY;
	if (random){
	    randomSprite->render(position.x + characterOffset.x, position.y + characterOffset.y, bmp,effects);
	} else {
	    character->getIcon()->render(position.x + characterOffset.x, position.y + characterOffset.y, bmp,effects);
	}
    }
    if (flash){
	Bitmap::drawingMode(Bitmap::MODE_TRANS);
	Bitmap::transBlender( 0, 0, 0, int(25.5 * flash) );
	bmp.rectangleFill( position.x -1, position.y -1, (position.x -1) + dimensions.x, (position.y - 1) + dimensions.y,Bitmap::makeColor(255,255,255));
	Bitmap::drawingMode(Bitmap::MODE_SOLID);
    }
}

Grid::Grid():
rows(0),
columns(0),
wrapping(false),
showEmptyBoxes(false),
moveOverEmptyBoxes(false),
cellSpacing(0),
cellBackgroundSprite(0),
cellRandomSprite(0),
cellRandomSwitchTime(0),
randomSwitchTimeTicker(0),
portraitScaleX(1),
portraitScaleY(1),
type(Mugen::Arcade){
}

Grid::~Grid(){
    // Destroy cell map
    for (CellMap::iterator i = cells.begin(); i != cells.end(); ++i){
	std::vector< Cell *> &row = (*i);
	for (std::vector< Cell *>::iterator column = row.begin(); column != row.end(); ++column){
	    Cell *cell = (*column);
	    if (cell){
		delete cell;
	    }
	}
    }
}

void Grid::initialize(){
    Mugen::Point currentPosition;
    currentPosition.y = position.y;
    for (int row = 0; row < rows; ++row){
	currentPosition.x = position.x;
	std::vector< Cell *> cellRow;
	for (int column = 0; column < columns; ++column){
	    Cell *cell = new Cell(row,column);
	    cell->setBackground(cellBackgroundSprite);
	    cell->setRandomSprite(cellRandomSprite);
	    cell->setPosition(currentPosition.x,currentPosition.y);
	    cell->setDimensions(cellSize.x,cellSize.y);
	    cell->setCharacterOffset(portraitOffset.x, portraitOffset.y);
	    cell->setCharacterScale(portraitScaleX, portraitScaleY);
            // Set random cell with a default character
            if (cell->isRandom()){
                cell->setCharacter(characters.front());
            }
	    cellRow.push_back(cell);
	    currentPosition.x += cellSize.x + cellSpacing;
	}
	cells.push_back(cellRow);
	currentPosition.y += cellSize.y + cellSpacing;
    }
}

void Grid::act(Cursor & player1Cursor, Cursor & player2Cursor){
    for (CellMap::iterator i = cells.begin(); i != cells.end(); ++i){
	std::vector< Cell *> &row = (*i);
	for (std::vector< Cell *>::iterator column = row.begin(); column != row.end(); ++column){
	    Cell *cell = (*column);
	    cell->act();
            if (randomSwitchTimeTicker == cellRandomSwitchTime){
                if (player1Cursor.getState() == Cursor::CharacterSelect){
                    if (player1Cursor.getCurrentCell() == cell && cell->isRandom()){
                        cell->randomize(characters);
                        player1Cursor.playRandomSound();
                    }
                }
                if (player2Cursor.getState() == Cursor::CharacterSelect){
                    if (player2Cursor.getCurrentCell() == cell && cell->isRandom()){
                        cell->randomize(characters);
                        player2Cursor.playRandomSound();
                    }
                }
            }
	}
    }

    if (randomSwitchTimeTicker == cellRandomSwitchTime){
        randomSwitchTimeTicker = 0;
    }
    randomSwitchTimeTicker++;

    stages.getFontHandler().act();
}

void Grid::render(const Bitmap & bmp){
    for (CellMap::iterator i = cells.begin(); i != cells.end(); ++i){
	std::vector< Cell *> &row = (*i);
	for (std::vector< Cell *>::iterator column = row.begin(); column != row.end(); ++column){
	    Cell *cell = (*column);
	    if (cell->isEmpty()){
		if (showEmptyBoxes){
		    cell->render(bmp);
		}
	    } else {
		cell->render(bmp);
	    }
	}
    }
}

void Grid::addCharacter(CharacterInfo *character, bool isRandom){
    for (CellMap::iterator i = cells.begin(); i != cells.end(); ++i){
	std::vector< Cell *> &row = (*i);
	for (std::vector< Cell *>::iterator column = row.begin(); column != row.end(); ++column){
	    Cell *cell = (*column);
	    if (cell->isEmpty()){
		if (isRandom){
		    cell->setRandom(true);
                    cell->setCharacter(character);
		    return;
		}
		character->setReferenceCell(cell);
		cell->setCharacter(character);
		characters.push_back(character);
		return;
	    } 
	}
    }
}

void Grid::setCursorStart(Cursor &cursor){
    Cell *cell = getCell(cursor.getStart().x,cursor.getStart().y);
    cell->setCursorState(Cell::One);
    cursor.setCurrentCell(cell);
}

void Grid::moveCursorLeft(Cursor &cursor){
    Mugen::Point current = cursor.getCurrentCell()->getLocation();
    current.y--;
    if (current.y < 0){
	if (wrapping){
	    current.y = columns-1;
	} else {
	    return;
	}
    }
    Cell *cell;
    try {
	cell = getCell(current.x,current.y);
    } catch (const MugenException &me){
	// Shouldn't happen but you never know lets not continue
	return;
    }
    
    if (cell->isEmpty()){
	if (!moveOverEmptyBoxes){
	    return;
	}
    }
    cursor.getCurrentCell()->popCursor();
    cell->pushCursor();
    cursor.setCurrentCell(cell);
    cursor.playMoveSound();
}

void Grid::moveCursorRight(Cursor &cursor){
    Mugen::Point current = cursor.getCurrentCell()->getLocation();
    current.y++;
    if (current.y >= columns){
	if (wrapping){
	    current.y = 0;
	} else {
	    return;
	}
    }
    Cell *cell;
    try {
	cell = getCell(current.x,current.y);
    } catch (const MugenException &me){
	// Shouldn't happen but you never know lets not continue
	return;
    }
    
    if (cell->isEmpty()){
	if (!moveOverEmptyBoxes){
	    return;
	}
    }
    cursor.getCurrentCell()->popCursor();
    cell->pushCursor();
    cursor.setCurrentCell(cell);
    cursor.playMoveSound();
}

void Grid::moveCursorUp(Cursor &cursor){
    Mugen::Point current = cursor.getCurrentCell()->getLocation();
    current.x--;
    if (current.x < 0){
	if (wrapping){
	    current.x = rows-1;
	} else {
	    return;
	}
    }
    Cell *cell;
    try {
	cell = getCell(current.x,current.y);
    } catch (const MugenException &me){
	// Shouldn't happen but you never know lets not continue
	return;
    }
    
    if (cell->isEmpty()){
	if (!moveOverEmptyBoxes){
	    return;
	}
    }
    cursor.getCurrentCell()->popCursor();
    cell->pushCursor();
    cursor.setCurrentCell(cell);
    cursor.playMoveSound();
}

void Grid::moveCursorDown(Cursor &cursor){
    Mugen::Point current = cursor.getCurrentCell()->getLocation();
    current.x++;
    if (current.x >= rows){
	if (wrapping){
	    current.x = 0;
	} else {
	    return;
	}
    }
    Cell *cell;
    try {
	cell = getCell(current.x,current.y);
    } catch (const MugenException &me){
	// Shouldn't happen but you never know lets not continue
	return;
    }
    
    if (cell->isEmpty()){
	if (!moveOverEmptyBoxes){
	    return;
	}
    }
    cursor.getCurrentCell()->popCursor();
    cell->pushCursor();
    cursor.setCurrentCell(cell);
    cursor.playMoveSound();
}

void Grid::selectCell(Cursor &cursor, const Mugen::Keys & key){
    // *TODO use the key to determine which map(act) is used
    // Get the appropriate cell for flashing in case of random
    cursor.getCurrentCell()->getCharacter()->getReferenceCell()->startFlash();
    // set cursor state depending on state
    switch (type){
	case Arcade:
	    cursor.setState(Cursor::Done);
            cursor.playSelectSound();
	    break;
	case Versus:
	    stages.setDisplay(true);
	    cursor.setState(Cursor::StageSelect);
            cursor.playSelectSound();
	    break;
	case TeamArcade:
	    break;
	case TeamVersus:
	    break;
	case TeamCoop:
	    break;
	case Survival:
	    break;
	case SurvivalCoop:
	    break;
	case Training:
	    break;
	case Watch:
	    break;
	default:
	    break;
    }
}

void Grid::selectStage(){
    // Set stage so that doesn't infinitely toggle
    if (stages.isSelecting()){
	stages.toggleSelecting();
    }
}

Cell *Grid::getCell(int row, int column) throw (MugenException){
    for (CellMap::iterator i = cells.begin(); i != cells.end(); ++i){
	std::vector< Cell *> &rowIterator = (*i);
	for (std::vector< Cell *>::iterator columnIterator = rowIterator.begin(); columnIterator != rowIterator.end(); ++columnIterator){
	    Cell *cell = (*columnIterator);
	    if (cell->getLocation() == Mugen::Point(row,column)){
		return cell;
	    }
	}
    }
    
    throw MugenException("Could not find cell.");
}

Cursor::Cursor():
activeSprite(0),
doneSprite(0),
blink(false),
blinkRate(10),
blinkCounter(0),
hideForBlink(false),
faceScaleX(0),
faceScaleY(0),
facing(0),
state(NotActive),
moveSound(0),
selectSound(0),
randomSound(0),
cancelRandom(false){
}

Cursor::~Cursor(){
}

void Cursor::act(Grid &grid){
    InputMap<Mugen::Keys>::Output out = InputManager::getMap(input);
    switch (state){
	case NotActive:
	    return;
	    break;
	case TeamSelect:
	    if (out[Up]){
	    }
	    if (out[Down]){
	    }
	    if (out[Left]){
	    }
	    if (out[Right]){
	    }
	    if (out[A]){
	    }
	    if (out[B]){
	    }
	    if (out[C]){
	    }
	    if (out[X]){
	    }
	    if (out[Y]){
	    }
	    if (out[Z]){
	    }
	    if (out[Start]){
	    }
	    break;
	case CharacterSelect:
	    if (out[Up]){
		grid.moveCursorUp(*this);
	    }
	    if (out[Down]){
		grid.moveCursorDown(*this);
	    }
	    if (out[Left]){
		grid.moveCursorLeft(*this);
	    }
	    if (out[Right]){
		grid.moveCursorRight(*this);
	    }

            if (!getCurrentCell()->isEmpty()){
                    Mugen::Keys selectable[] = {A, B, C, X, Y, Z};
                for (unsigned int key = 0; key < sizeof(selectable) / sizeof(Mugen::Keys); key++){
                    if (out[selectable[key]]){
                        grid.selectCell(*this, selectable[key]);
                    }
                }
            }
            /*
	    if (out[A]){
		grid.selectCell(*this,A);
	    }
	    if (out[B]){
		grid.selectCell(*this,B);
	    }
	    if (out[C]){
		grid.selectCell(*this,C);
	    }
	    if (out[X]){
		grid.selectCell(*this,X);
	    }
	    if (out[Y]){
		grid.selectCell(*this,Y);
	    }
	    if (out[Z]){
		grid.selectCell(*this,Z);
	    }
            */

	    if (out[Start]){
	    }
	    if (blink && (currentCell->getCursorState() == Cell::Two)){
		blinkCounter++;
		if (blinkCounter > blinkRate){
		    blinkCounter = 0;
		    hideForBlink = !hideForBlink;
		}
	    } else {
		hideForBlink = false;
	    }
	    break;
	case StageSelect:
	    /* Check if selecting is still possible else set done state */
	    if (!grid.getStageHandler().isSelecting()){
		state = Done;
	    }
	    if (out[Left]){
		grid.getStageHandler().prev();
	    }
	    if (out[Right]){
		grid.getStageHandler().next();
	    }
	    if (out[A]){
		grid.selectStage();
	    }
	    if (out[B]){
		grid.selectStage();
	    }
	    if (out[C]){
		grid.selectStage();
	    }
	    if (out[X]){
		grid.selectStage();
	    }
	    if (out[Y]){
		grid.selectStage();
	    }
	    if (out[Z]){
		grid.selectStage();
	    }
	    if (out[Start]){
	    }
	    break;
	case Done:
	default:
	    break;
    }
}

void Cursor::render(Grid &grid, const Bitmap & bmp){
    switch (state){
	case NotActive:
	    return;
	    break;
	case TeamSelect:
	    break;
	case CharacterSelect:
	    if (!hideForBlink){
		activeSprite->render(currentCell->getPosition().x,currentCell->getPosition().y,bmp);
	    }
	    renderPortrait(bmp);
	    break;
	case Done:
	default:
	    if (!hideForBlink){
		doneSprite->render(currentCell->getPosition().x,currentCell->getPosition().y,bmp);
	    }
	    renderPortrait(bmp);
    }
    
    /* Have to make sure stage select is prominent kinda stupid */
    grid.getStageHandler().render(bmp);
}

void Cursor::playMoveSound(){
    if (moveSound){
        moveSound->play();
    }
}

void Cursor::playSelectSound(){
    if (selectSound){
        selectSound->play();
    }
}

void Cursor::playRandomSound(){
    if (randomSound){
        if (cancelRandom){
            randomSound->stop();
        }
        randomSound->play();
    } 
}

void Cursor::renderPortrait(const Bitmap &bmp){
    // Lets do the portrait and name
    if (!currentCell->isEmpty()){
	const CharacterInfo *character = currentCell->getCharacter();
	Mugen::Effects effects;
	effects.facing = facing;
	effects.scalex = faceScaleX;
	effects.scaley = faceScaleY;
	character->getPortrait()->render(faceOffset.x,faceOffset.y,bmp,effects);
	font.render(character->getName(),bmp);
    }
}

VersusScreen::VersusScreen():
background(0),
time(0){
}

VersusScreen::~VersusScreen(){
}

void VersusScreen::render(CharacterInfo & player1, CharacterInfo & player2, MugenStage * stage, const Bitmap &bmp){
    Bitmap workArea(DEFAULT_WIDTH,DEFAULT_HEIGHT);
    bool done = false;
    bool escaped = false;
    
    int ticker = 0;
    
    // Set the fade state
    fader.setState(FADEIN);
  
    double runCounter = 0;
    Global::speed_counter = 0;
    Global::second_counter = 0;
    int game_time = 100;
    
    // Set game keys temporary
    InputMap<Mugen::Keys> gameInput;
    gameInput.set(Keyboard::Key_ESC, 10, true, Mugen::Esc);
    
    while ( ! done && fader.getState() != RUNFADE ){
    
	bool draw = false;
	
	if ( Global::speed_counter > 0 ){
	    draw = true;
	    runCounter += Global::speed_counter * Global::LOGIC_MULTIPLIER;
	    while ( runCounter >= 1.0 ){
		// tick tock
		ticker++;
		
		runCounter -= 1;
		// Key handler
		InputManager::poll();

                if (Global::shutdown()){
                    throw ShutdownException();
                }
		
		InputMap<Mugen::Keys>::Output out = InputManager::getMap(gameInput);
		if (out[Mugen::Esc]){
		    done = escaped = true;
		    fader.setState(FADEOUT);
		    InputManager::waitForRelease(gameInput, Mugen::Esc);
		}
		
		// Logic
		if (ticker >= time){
		    pthread_t loader;
		    try{
			Level::LevelInfo info;
			info.setBackground(&bmp);
			info.setLoadingMessage("Loading...");
                        info.setPosition(-1, 400);
			Loader::startLoading(&loader, (void*) &info);
			// Load player 1
			player1.loadPlayer1();
			// Load player 2
			player2.loadPlayer2();
			// Load stage
			stage->addp1(player1.getPlayer1());
			stage->addp2(player2.getPlayer2());
			stage->load();
			Loader::stopLoading(loader);
		    } catch (const MugenException & e){
			Loader::stopLoading(loader);
			throw e;
		    }
		    done = true;
		    fader.setState(FADEOUT);
		}
		
		// Fader
		fader.act();
		
		// Backgrounds
		background->act();
		
		// Player fonts
		player1Font.act();
		player2Font.act();
	    }
	    
	    Global::speed_counter = 0;
	}
		
	while ( Global::second_counter > 0 ){
	    game_time--;
	    Global::second_counter--;
	    if ( game_time < 0 ){
		    game_time = 0;
	    }
	}

	if ( draw ){
	    // render backgrounds
	    background->renderBackground(0,0,workArea);
	    
	    // render portraits
	    player1.getPortrait()->render(player1Position.x,player1Position.y,workArea,player1Effects);
	    player2.getPortrait()->render(player2Position.x,player2Position.y,workArea,player2Effects);
	    
	    // render fonts
	    player1Font.render(player1.getName(),workArea);
	    player2Font.render(player2.getName(),workArea);
	    
	    // render Foregrounds
	    background->renderForeground(0,0,workArea);
	    
	    // render fades
	    fader.draw(workArea);
	    
	    // Finally render to screen
	    workArea.Stretch(bmp);
	    bmp.BlitToScreen();
	}

	while ( Global::speed_counter < 1 ){
		PaintownUtil::rest( 1 );
	}
    }
    
    // **FIXME Hack figure something out
    if (escaped){
	throw ReturnException();
    }
}

static std::vector<Ast::Section*> collectSelectStuff(Ast::AstParse::section_iterator & iterator, Ast::AstParse::section_iterator end){
    Ast::AstParse::section_iterator last = iterator;
    vector<Ast::Section*> stuff;

    Ast::Section * section = *iterator;
    std::string head = section->getName();
    /* better to do case insensitive regex matching rather than
     * screw up the original string
     */
    stuff.push_back(section);
    iterator++;

    while (true){
        if (iterator == end){
            break;
        }

        section = *iterator;
        string sectionName = section->getName();
        sectionName = Mugen::Util::fixCase(sectionName);
        // Global::debug(2, __FILE__) << "Match '" << (prefix + name + ".*") << "' against '" << sectionName << "'" << endl;
        if (PaintownUtil::matchRegex(sectionName, "select")){
            stuff.push_back(section);
        } else {
            break;
        }

        last = iterator;
        iterator++;
    }
    iterator = last;
    return stuff;
}

CharacterSelect::CharacterSelect(const std::string &file, const GameType &type):
systemFile(file),
sffFile(""),
sndFile(""),
selectFile(""),
type(type),
cancelSound(0),
currentPlayer1(0),
currentPlayer2(0),
currentStage(0){
    grid.setGameType(type);
    
    switch (type){
	case Arcade:
	    // set first cursor1
	    player1Cursor.setState(Cursor::CharacterSelect);
	    break;
	case Versus:
	    player1Cursor.setState(Cursor::CharacterSelect);
	    player2Cursor.setState(Cursor::CharacterSelect);
	    break;
	case TeamArcade:
	    player1Cursor.setState(Cursor::TeamSelect);
	    break;
	case TeamVersus:
	    player1Cursor.setState(Cursor::TeamSelect);
	    player2Cursor.setState(Cursor::TeamSelect);
	    break;
	case TeamCoop:
	    player1Cursor.setState(Cursor::CharacterSelect);
	    break;
	case Survival:
	    player1Cursor.setState(Cursor::TeamSelect);
	    break;
	case SurvivalCoop:
	    player1Cursor.setState(Cursor::CharacterSelect);
	    break;
	case Training:
	    player1Cursor.setState(Cursor::CharacterSelect);
	    break;
	case Watch:
	    break;
	default:
	    break;
    }
}

CharacterSelect::~CharacterSelect(){
    // Get rid of sprites
    for( Mugen::SpriteMap::iterator i = sprites.begin() ; i != sprites.end() ; ++i ){
      for( std::map< unsigned int, MugenSprite * >::iterator j = i->second.begin() ; j != i->second.end() ; ++j ){
	  if( j->second )delete j->second;
      }
    }
    // Get rid of sounds
    for( std::map< unsigned int, std::map< unsigned int, MugenSound * > >::iterator i = sounds.begin() ; i != sounds.end() ; ++i ){
      for( std::map< unsigned int, MugenSound * >::iterator j = i->second.begin() ; j != i->second.end() ; ++j ){
	  if( j->second )delete j->second;
      }
    }
    // Cleanup fonts
    for (std::vector< MugenFont *>::iterator f = fonts.begin(); f != fonts.end(); ++f){
	    if (*f) delete (*f);
    }
    /* background */
    if (background){
	delete background;
    }
    // Kill stage
    if (currentStage){
	delete currentStage;
    }
}

void CharacterSelect::load() throw (MugenException){
    // Lets look for our def since some people think that all file systems are case insensitive
    std::string baseDir = Mugen::Util::getFileDir(systemFile);
    
    Global::debug(1) << baseDir << endl;
    
    if (systemFile.empty()){
        throw MugenException( "Cannot locate character select definition file for: " + systemFile );
    }

    TimeDifference diff;
    diff.startTime();
    Ast::AstParse parsed((list<Ast::Section*>*) Mugen::Def::main(systemFile));
    diff.endTime();
    Global::debug(1) << "Parsed mugen file " + systemFile + " in" + diff.printTime("") << endl;
    
    for (Ast::AstParse::section_iterator section_it = parsed.getSections()->begin(); section_it != parsed.getSections()->end(); section_it++){
        Ast::Section * section = *section_it;
	std::string head = section->getName();
        /* this should really be head = Mugen::Util::fixCase(head) */
	head = Mugen::Util::fixCase(head);
        if (head == "info"){
	    /* Nothing right now */
        } else if (head == "files"){
            class FileWalker: public Ast::Walker{
                public:
                    FileWalker(Mugen::CharacterSelect & select, const string & baseDir):
                        select(select),
                        baseDir(baseDir){
                        }

                    Mugen::CharacterSelect & select;
                    const string & baseDir;

                    virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                        if (simple == "spr"){
                            simple >> select.sffFile;
                            Global::debug(1) << "Got Sprite File: '" << select.sffFile << "'" << endl;
                            Mugen::Util::readSprites(Mugen::Util::getCorrectFileLocation(baseDir, select.sffFile), "", select.sprites);
			    for( Mugen::SpriteMap::iterator i = select.sprites.begin() ; i != select.sprites.end() ; ++i ){
				// Load these sprites so they are ready to use
				for( std::map< unsigned int, MugenSprite * >::iterator j = i->second.begin() ; j != i->second.end() ; ++j ){
				    if( j->second )j->second->load();
				}
			    }
                        } else if (simple == "snd"){
                            simple >> select.sndFile;
                            Mugen::Util::readSounds( Mugen::Util::getCorrectFileLocation(baseDir, select.sndFile ), select.sounds);
                            Global::debug(1) << "Got Sound File: '" << select.sndFile << "'" << endl;
                        } else if (simple == "logo.storyboard"){
                            // Ignore
                        } else if (simple == "intro.storyboard"){
                            // Ignore
                        } else if (simple == "select"){
                            simple >> select.selectFile;
                            Global::debug(1) << "Got Select File: '" << select.selectFile << "'" << endl;
                        } else if (simple == "fight"){
                            // Ignore
                        } else if (PaintownUtil::matchRegex(simple.idString(), "^font")){
                            string temp;
                            simple >> temp;
                            select.fonts.push_back(new MugenFont(Mugen::Util::getCorrectFileLocation(baseDir, temp)));
                            Global::debug(1) << "Got Font File: '" << temp << "'" << endl;

                        } else {
                            throw MugenException("Unhandled option in Files Section: " + simple.toString(), __FILE__, __LINE__ );
                        }
                    }
            };
            
            FileWalker walker(*this, baseDir);
            section->walk(walker);
        } else if (head == "title info"){
	    /* Nothing */
	} else if (PaintownUtil::matchRegex(head, "^titlebg")){
	    /* Nothing */
	} else if (head == "select info"){ 
            class SelectInfoWalker: public Ast::Walker{
            public:
                SelectInfoWalker(CharacterSelect & self, Mugen::SpriteMap & sprites):
                    self(self),
                    sprites(sprites){
                    }

                CharacterSelect & self;
                Mugen::SpriteMap & sprites;

                virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
		if (simple == "fadein.time" ){
		    int time;
                    simple >> time;
		    self.fader.setFadeInTime(time);
		} else if (simple == "fadein.color" ){
		    int r,g,b;
                    simple >> r >> g >> b;
		    self.fader.setFadeInColor(Bitmap::makeColor(r,g,b));
		} else if (simple == "fadeout.time"){
		    int time;
                    simple >> time;
		    self.fader.setFadeOutTime(time);
		} else if (simple == "fadeout.color"){
		    int r,g,b;
                    simple >> r >> g >> b;
		    self.fader.setFadeOutColor(Bitmap::makeColor(r,g,b));
		} else if (simple == "rows"){
                    int rows;
		    simple >> rows;
		    self.grid.setRows(rows);
		} else if (simple == "columns"){
                    int columns;
		    simple >> columns;
		    self.grid.setColumns(columns);
		} else if (simple == "wrapping"){
		    bool wrap;
                    simple >> wrap;
		    self.grid.setWrapping(wrap);
		} else if (simple == "pos"){
		    int x,y;
                    simple >> x >> y;
		    self.grid.setPosition(x,y);
		} else if (simple == "showemptyboxes"){
                    bool boxes;
		    simple >> boxes;
		    self.grid.setShowEmptyBoxes(boxes);
		} else if (simple == "moveoveremptyboxes"){
		    bool boxes;
                    simple >> boxes;
		    self.grid.setMoveOverEmptyBoxes(boxes);
		} else if (simple == "cell.size"){
                    int x, y;
		    simple >> x >> y;
		    self.grid.setCellSize(x,y);
		} else if (simple == "cell.spacing"){
		    int spacing;
                    simple >> spacing;
		    self.grid.setCellSpacing(spacing);
		} else if (simple == "cell.bg.spr"){
		    int group, sprite;
                    simple >> group >> sprite;
		    self.grid.setCellBackgroundSprite(sprites[group][sprite]);
		} else if (simple == "cell.random.spr"){
		    int group, sprite;
                    simple >> group >> sprite;
		    self.grid.setCellRandomSprite(sprites[group][sprite]);
		} else if (simple == "cell.random.switchtime"){
		    int time;
		    simple >> time;
		    self.grid.setCellRandomSwitchTime(time);
		} else if (simple == "p1.cursor.startcell"){
		    int x,y;
                    simple >> x >> y;
		    self.player1Cursor.setStart(x,y);
		} else if (simple == "p1.cursor.active.spr"){
		    int group, sprite;
		    simple >> group >> sprite;
		    self.player1Cursor.setActiveSprite(sprites[group][sprite]);
		} else if (simple == "p1.cursor.done.spr"){
		    int group, sprite;
                    simple >> group >> sprite;
		    self.player1Cursor.setDoneSprite(sprites[group][sprite]);
		} else if (simple == "p1.cursor.move.snd"){
                   try{
                        int group, sound;
                        simple >> group >> sound;
                        self.player1Cursor.setMoveSound(self.sounds[group][sound]);
                   } catch (const Ast::Exception & e){
                   }
                } else if (simple == "p1.cursor.done.snd"){
                    try{
                        int group, sound;
                        simple >> group >> sound;
                        self.player1Cursor.setSelectSound(self.sounds[group][sound]);
                   } catch (const Ast::Exception & e){
                   }
                } else if (simple == "p1.random.move.snd"){
                    try{
                        int group, sound;
                        simple >> group >> sound;
                        self.player1Cursor.setRandomSound(self.sounds[group][sound]);
                   } catch (const Ast::Exception & e){
                   }
                } else if (simple == "p2.cursor.startcell"){
                    int x,y;
                    simple >> x >> y;
		    self.player2Cursor.setStart(x,y);
		} else if (simple == "p2.cursor.active.spr"){
		    int group, sprite;
                    simple >> group >> sprite;
		    self.player2Cursor.setActiveSprite(sprites[group][sprite]);
		} else if (simple == "p2.cursor.done.spr"){
		    int group, sprite;
                    simple >> group >> sprite;
		    self.player2Cursor.setDoneSprite(sprites[group][sprite]);
		} else if (simple == "p2.cursor.blink"){
		    bool blink;
		    simple >> blink;
		    self.player2Cursor.setBlink(blink);
		} 
		else if ( simple == "p2.cursor.move.snd"){ 
                    try{
                        int group, sound;
                        simple >> group >> sound;
                        self.player2Cursor.setMoveSound(self.sounds[group][sound]);
                   } catch (const Ast::Exception & e){
                   }
                } else if ( simple == "p2.cursor.done.snd"){
                    try{
                        int group, sound;
                        simple >> group >> sound;
                        self.player2Cursor.setSelectSound(self.sounds[group][sound]);
                   } catch (const Ast::Exception & e){
                   }
                } else if ( simple == "p2.random.move.snd"){
                   try{
                        int group, sound;
                        simple >> group >> sound;
                        self.player2Cursor.setRandomSound(self.sounds[group][sound]);
                   } catch (const Ast::Exception & e){
                   }
                } else if (simple == "random.move.snd.cancel"){
                   bool cancel;
                   simple >> cancel;
                   self.player1Cursor.setRandomCancel(cancel);
                   self.player2Cursor.setRandomCancel(cancel);
                } else if ( simple == "stage.move.snd"){
                   try{
                        int group, sound;
                        simple >> group >> sound;
                        self.grid.getStageHandler().setMoveSound(self.sounds[group][sound]);
                   } catch (const Ast::Exception & e){
                   }
                } else if ( simple == "stage.done.snd"){
                   try{
                        int group, sound;
                        simple >> group >> sound;
                        self.grid.getStageHandler().setSelectSound(self.sounds[group][sound]);
                   } catch (const Ast::Exception & e){
                   }
                } else if ( simple == "cancel.snd"){
                   try{
                        int group, sound;
                        simple >> group >> sound;
                        self.cancelSound = self.sounds[group][sound];
                   } catch (const Ast::Exception & e){
                   }
                } else if (simple == "portrait.offset"){
		    int x,y;
		    simple >> x >> y;
		    self.grid.setPortraitOffset(x,y);
		} else if (simple == "portrait.scale"){
		    double x,y;
		    simple >> x >> y;
		    self.grid.setPortraitScale(x,y);
		} else if ( simple == "title.offset"){
		    int x, y;
		    simple >> x >> y;
		    self.titleFont.setLocation(x,y);
		} else if ( simple == "title.font"){
		    int index=0, bank=0, position=0;
		    try {
			simple >> index >> bank >> position;
		    } catch (const Ast::Exception & e){
			//ignore for now
		    }
		    self.titleFont.setPrimary(self.fonts[index-1],bank,position);
		} else if ( simple == "p1.face.offset"){
		    int x, y;
		    simple >> x >> y;
		    self.player1Cursor.setFaceOffset(x,y);
		} else if ( simple == "p1.face.scale"){
		    double x, y;
		    simple >> x >> y;
		    self.player1Cursor.setFaceScale(x,y);
		} else if ( simple == "p1.face.facing"){
		    int f;
		    simple >> f;
		    self.player1Cursor.setFacing(f);
		} else if ( simple == "p2.face.offset"){
		    int x, y;
		    simple >> x >> y;
		    self.player2Cursor.setFaceOffset(x,y);
		} else if ( simple == "p2.face.scale"){
		    double x, y;
		    simple >> x >> y;
		    self.player2Cursor.setFaceScale(x,y);
		} else if ( simple == "p2.face.facing"){
		    int f;
		    simple >> f;
		    self.player2Cursor.setFacing(f);
		} else if ( simple == "p1.name.offset"){
		    int x, y;
		    simple >> x >> y;
		    self.player1Cursor.getFontHandler().setLocation(x,y);
		}  else if ( simple == "p1.name.font"){
		    int index=0, bank=0, position=0;
		    try {
			simple >> index >> bank >> position;
		    } catch (const Ast::Exception & e){
			//ignore for now
		    } self.player1Cursor.getFontHandler().setPrimary(self.fonts[index-1],bank,position);
		} else if ( simple == "p2.name.offset"){
		    int x, y;
		    simple >> x >> y;
		    self.player2Cursor.getFontHandler().setLocation(x,y);
		} else if ( simple == "p2.name.font"){
		    int index, bank, position;
		    simple >> index >> bank >> position;
		    self.player2Cursor.getFontHandler().setPrimary(self.fonts[index-1],bank,position);
		} else if ( simple == "stage.pos"){
		    int x, y;
		    simple >> x >> y;
		    self.grid.getStageHandler().getFontHandler().setLocation(x,y);
		} else if ( simple == "stage.active.font"){
		    int index=0, bank=0, position=0;
		    try {
			simple >> index >> bank >> position;
		    } catch (const Ast::Exception & e){
			//ignore for now
		    }
		    self.grid.getStageHandler().getFontHandler().setPrimary(self.fonts[index-1],bank,position);
		} else if ( simple == "stage.active2.font"){
                    int index=0, bank=0, position=0;
		    try {
			simple >> index >> bank >> position;
		    } catch (const Ast::Exception & e){
			//ignore for now
		    }
		    self.grid.getStageHandler().getFontHandler().setBlink(self.fonts[index-1],bank,position);
		} else if ( simple == "stage.done.font"){
                    int index=0, bank=0, position=0;
		    try {
			simple >> index >> bank >> position;
		    } catch (const Ast::Exception & e){
			//ignore for now
		    }
		    self.grid.getStageHandler().getFontHandler().setDone(self.fonts[index-1],bank,position);
		}
#if 0
                else if ( simple.find("teammenu")!=std::string::npos ){
                    /* Ignore for now */
                }
#endif
		//else throw MugenException( "Unhandled option in Select Info Section: " + itemhead );
                }
            };

            SelectInfoWalker walker(*this, sprites);
            section->walk(walker);
	} else if (head == "selectbgdef"){ 
	    /* Background management */
	    Mugen::Background *manager = new Mugen::Background(systemFile, "selectbg");
	    background = manager;
	} else if (head.find("selectbg") != std::string::npos ){ /* Ignore for now */ }
	else if (head == "vs screen" ){
	    class VersusWalker: public Ast::Walker{
            public:
                VersusWalker(VersusScreen & self, std::vector<MugenFont *> & fonts):
                    self(self),
		    fonts(fonts){
                    }

                VersusScreen & self;
		std::vector<MugenFont *> & fonts;

                virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
		    if (simple == "time" ){
			int time;
			simple >> time;
			self.setTime(time);
		    } else if (simple == "fadein.time"){
			int time;
			simple >> time;
			self.getFadeTool().setFadeInTime(time);
		    } else if (simple == "fadeout.time"){
			int time;
			simple >> time;
			self.getFadeTool().setFadeOutTime(time);
		    } else if (simple == "p1.pos"){
			int x=0,y=0;
			try{
			    simple >> x >> y;
			} catch (Ast::Exception & e){
			}
			self.setPlayer1Position(Mugen::Point(x,y));
		    } else if (simple == "p1.facing"){
			int face;
			simple >> face;
			self.setPlayer1Facing(face);
		    } else if (simple == "p1.scale"){
			double x,y;
			try{
			    simple >> x >> y;
			} catch (Ast::Exception & e){
			}
			self.setPlayer1Scale(x,y);
		    } else if (simple == "p2.pos"){
			int x=0,y=0;
			try{
			    simple >> x >> y;
			} catch (Ast::Exception & e){
			}
			self.setPlayer2Position(Mugen::Point(x,y));
		    } else if (simple == "p2.facing"){
			int face;
			simple >> face;
			self.setPlayer2Facing(face);
		    } else if (simple == "p2.scale"){
			double x,y;
			try{
			    simple >> x >> y;
			} catch (Ast::Exception & e){
			}
			self.setPlayer2Scale(x,y);
		    } else if (simple == "p1.name.pos"){
			int x, y;
			try {
			    simple >> x >> y;
			} catch (Ast::Exception & e){
			}
			self.getPlayer1Font().setLocation(x,y);
		    } else if (simple == "p1.name.font"){
			int index=0, bank=0, position=0;
			try {
			    simple >> index >> bank >> position;
			} catch (const Ast::Exception & e){
			    //ignore for now
			}
			self.getPlayer1Font().setPrimary(fonts[index-1],bank,position);
		    } else if (simple == "p2.name.pos"){
			int x, y;
			try {
			    simple >> x >> y;
			} catch (Ast::Exception & e){
			}
			self.getPlayer2Font().setLocation(x,y);
		    } else if (simple == "p2.name.font"){
			int index=0, bank=0, position=0;
			try {
			    simple >> index >> bank >> position;
			} catch (const Ast::Exception & e){
			    //ignore for now
			}
			self.getPlayer2Font().setPrimary(fonts[index-1],bank,position);
		    }
		}
	    };
	    
            VersusWalker walker(versus,fonts);
            section->walk(walker);
	}
	else if (head == "versusbgdef" ){
	    /* Background management */
	    Mugen::Background *manager = new Mugen::Background(systemFile, "versusbg");
	    versus.setBackground(manager);
	}
	else if (head.find("versusbg" ) != std::string::npos ){ /* Ignore for now */ }
	else if (head == "demo mode" ){ /* Ignore for now */ }
	else if (head == "continue screen" ){ /* Ignore for now */ }
	else if (head == "game over screen" ){ /* Ignore for now */ }
	else if (head == "win screen" ){ /* Ignore for now */ }
	else if (head == "default ending" ){ /* Ignore for now */ }
	else if (head == "end credits" ){ /* Ignore for now */ }
	else if (head == "survival results screen" ){ /* Ignore for now */ }
	else if (head == "option info" ){ /* Ignore for now */ }
	else if (head == "optionbgdef" ){ /* Ignore for now */ }
	else if (head.find("optionbg") != std::string::npos ){ /* Ignore for now */ }
	else if (head == "music" ){ /* Ignore for now */ }
	else if (head.find("begin action") != std::string::npos ){ /* Ignore for now */ }
        else {
            throw MugenException("Unhandled Section in '" + systemFile + "': " + head, __FILE__, __LINE__ ); 
        }
    }
    
    // Set up Grid
    grid.initialize();
    
    // Setup cursors
    grid.setCursorStart(player1Cursor);
    grid.setCursorStart(player2Cursor);
    
    // Now load up our characters
    parseSelect(Mugen::Util::fixFileName( baseDir, Mugen::Util::stripDir(selectFile)));
}

//! Get group of characters by order number
std::vector<CharacterInfo *> CharacterSelect::getCharacterGroup(int orderNumber){
    std::vector<CharacterInfo *> tempCharacters;
    for (std::vector<CharacterInfo *>::iterator i = characters.begin(); i != characters.end(); ++i){
	CharacterInfo *character = *i;
	if (character->getOrder() == orderNumber){
	    tempCharacters.push_back(character);
	}
    }
    return tempCharacters;
}

class CharacterCollect{
    public:
	CharacterCollect():
	random(false),
	randomStage(false),
	name(""),
	stage(""),
	includeStage(true),
	order(1),
	song(""){
	}
	~CharacterCollect(){
	}
	bool random;
	bool randomStage;
	std::string name;
	std::string stage;
	bool includeStage;
	int order;
	std::string song;
};

void CharacterSelect::parseSelect(const std::string &selectFile){
    const std::string file = Mugen::Util::getCorrectFileLocation(Mugen::Util::getFileDir(selectFile), Mugen::Util::stripDir(selectFile));
    
    TimeDifference diff;
    diff.startTime();
    Ast::AstParse parsed((list<Ast::Section*>*) Mugen::Def::main(file));
    diff.endTime();
    Global::debug(1) << "Parsed mugen file " + file + " in" + diff.printTime("") << endl;
    
    // Characters
    std::vector< CharacterCollect > characterCollection;
    // Stages
    std::vector< std::string > stageNames;
    
    // Arcade max matches
    std::vector<int> arcadeMaxMatches;
    // Team max matches
    std::vector<int> teamMaxMatches;
    
    for (Ast::AstParse::section_iterator section_it = parsed.getSections()->begin(); section_it != parsed.getSections()->end(); section_it++){
	Ast::Section * section = *section_it;
	std::string head = section->getName();
        
	head = Mugen::Util::fixCase(head);

        if (head == "characters"){
            class CharacterWalker: public Ast::Walker{
            public:
		CharacterWalker(std::vector< CharacterCollect > & characters):
		characters(characters){}
		virtual ~CharacterWalker(){}
		
		std::vector< CharacterCollect > &characters;
                virtual void onValueList(const Ast::ValueList & list){
                    CharacterCollect character;
		    // Grab Character
		    std::string temp;
		    list >> temp;
		    if (temp == "randomselect"){
			character.random = true;
		    } else {
			character.name = temp;
		    }

                    try{
                        // Grab stage
                        list >> temp;
                        if (temp == "random"){
                            character.randomStage = true;
                        } else {
                            character.stage = temp;
                        }
                        // Grab options
                        /* TODO: make the parser turn these into better AST nodes.
                         * something like Assignment(Id(music), Filename(whatever))
                         */
                        while(true){
                            list >> temp;
                            if (PaintownUtil::matchRegex(temp,"includestage=")){
                                temp.replace(0,std::string("includestage=").size(),"");
                                character.includeStage = (bool)atoi(temp.c_str());
                            } else if (PaintownUtil::matchRegex(temp,"music=")){
                                temp.replace(0,std::string("music=").size(),"");
                                character.song = temp;
                            } else if (PaintownUtil::matchRegex(temp,"order=")){
                                temp.replace(0,std::string("order=").size(),"");
                                character.order = (bool)atoi(temp.c_str());
                            }
                        }
                    } catch (const Ast::Exception & e){
                    }

		    characters.push_back(character);
                }
            };

            CharacterWalker walk(characterCollection);
            section->walk(walk);
	} else if (head == "extrastages"){
	    class StageWalker: public Ast::Walker{
            public:
		StageWalker(std::vector< std::string > &names):
		names(names){
		}
		virtual ~StageWalker(){}
		std::vector< std::string > &names;
                virtual void onValueList(const Ast::ValueList & list){
		    // Get Stage info and save it
		    try {
			std::string temp;
			list >> temp;
			Global::debug(0) << "stage: " << temp << endl;
			names.push_back(temp);
		    } catch (...){
		    }
                }
            };
	    StageWalker walk(stageNames);
	    section->walk(walk);
	} else if (head == "options"){
	    class OptionWalker: public Ast::Walker{
            public:
		OptionWalker(std::vector<int> & arcade, std::vector<int> & team):
		arcade(arcade),
		team(team){
		}
		virtual ~OptionWalker(){}
		std::vector<int> & arcade;
		std::vector<int> & team;
                virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
		    if (simple == "arcade.maxmatches"){
			// only 10 max matches
			for (int i = 0; i < 10; ++i){
			    try{
				int matches;
				simple >> matches;
				// No need to save the remaining of the matchup setup
				if (matches == 0){
				    break;
				}
				arcade.push_back(matches);
			    } catch (const Ast::Exception & e){
				break;
			    }
			}
		    } else if (simple == "team.maxmatches"){
			// only 10 max matches
			for (int i = 0; i < 10; ++i){
			    try{
				int matches;
				simple >> matches;
				// No need to save the remaining of the matchup setup
				if (matches == 0){
				    break;
				}
				team.push_back(matches);
			    } catch (const Ast::Exception & e){
				break;
			    }
			}
		    }
		}
            };
	    OptionWalker walk(arcadeMaxMatches,teamMaxMatches);
	    section->walk(walk);
	} else {
	    throw MugenException("Unhandled Section in '" + file + "': " + head, __FILE__, __LINE__); 
	}
    }
    
    // Set up our characters along the grid (excluding random select)
    // Offset for stage placement
    int stageOffset = 0;
    for (std::vector<CharacterCollect>::iterator i = characterCollection.begin(); i != characterCollection.end();++i){
	CharacterCollect & character = *i;
	if (!character.random){
	    // Get character
	    // *FIXME Not an elegant solution for character location
	    const std::string baseDir = Filesystem::find("mugen/chars/" + character.name + "/");
	    std::string str = Mugen::Util::stripDir(character.name);
	    const std::string charDefFile = Mugen::Util::fixFileName(baseDir, std::string(str + ".def"));
	    Global::debug(0) << "Got character def: " << charDefFile << endl;
	    CharacterInfo *charInfo = new CharacterInfo(charDefFile);
	    charInfo->setRandomStage(character.randomStage);
	    // Set stage
	    if (character.stage.empty()){
		// lets assume random then
		charInfo->setRandomStage(true);
	    } else {
		// Fix the stage name before handing it the character
		charInfo->setStage(fixStageName(character.stage));
		// also add the stage
		if (character.includeStage){
		    // Pass base stage name, StageHandler will fix the stage name
		    stageNames.insert(stageNames.begin()+stageOffset,character.stage);
		    stageOffset++;
		}
	    }
	    charInfo->setOrder(character.order);
	    charInfo->setMusic(character.song);
	    characters.push_back(charInfo);
	}
    }
    
    // Now setup Grid
    std::vector<CharacterInfo *>::iterator nextChar = characters.begin();
    for (std::vector<CharacterCollect>::iterator i = characterCollection.begin(); i != characterCollection.end();++i){
	CharacterCollect & character = *i;
	if (character.random){
	    grid.addCharacter(characters.front(), true);
	} else {
	    grid.addCharacter(*nextChar);
	    nextChar++;
	}
    }
    
    // Prepare stages
    for (std::vector<std::string>::iterator i = stageNames.begin(); i != stageNames.end(); ++i){
	grid.getStageHandler().addStage((*i));
    }
    
    // Setup arcade matches
    int order = 1;
    for (std::vector<int>::iterator i =  arcadeMaxMatches.begin(); i != arcadeMaxMatches.end();++i){
	std::vector<CharacterInfo *> tempCharacters = getCharacterGroup(order);
	std::random_shuffle(tempCharacters.begin(),tempCharacters.end());
	std::vector<CharacterInfo *>::iterator currentCharacter = tempCharacters.begin();
	std::queue<CharacterInfo *> characters;
	for (int m = 0; m < *i; ++m){
	    if (currentCharacter != tempCharacters.end()){
		characters.push(*currentCharacter);
		currentCharacter++;
	    } else {
		// No more
		break;
	    }
	}
	if (!characters.empty()){
	    arcadeMatches.push(characters);
	}
	order++;
    }
    
    // Setup team matches
    order = 1;
    for (std::vector<int>::iterator i =  teamMaxMatches.begin(); i != teamMaxMatches.end();++i){
	std::vector<CharacterInfo *> tempCharacters = getCharacterGroup(order);
	std::random_shuffle(tempCharacters.begin(),tempCharacters.end());
	std::vector<CharacterInfo *>::iterator currentCharacter = tempCharacters.begin();
	std::queue<CharacterInfo *> characters;
	for (int m = 0; m < *i; ++m){
	    if (currentCharacter != tempCharacters.end()){
		characters.push(*currentCharacter);
		currentCharacter++;
	    } else {
		// No more
		break;
	    }
	}
	if (!characters.empty()){
	    teamMatches.push(characters);
	}
	order++;
    }
}

void CharacterSelect::run(const std::string & title, const Bitmap &bmp){
    Bitmap workArea(DEFAULT_WIDTH,DEFAULT_HEIGHT);
    bool done = false;
    bool escaped = false;
    
    // Set the fade state
    fader.setState(FADEIN);
  
    double runCounter = 0;
    Global::speed_counter = 0;
    Global::second_counter = 0;
    int game_time = 100;
    
    // Set game keys temporary
    InputMap<Mugen::Keys> gameInput = Mugen::getPlayer1MenuKeys();
    
    while ( ! done && fader.getState() != RUNFADE ){
    
	bool draw = false;
	
	if ( Global::speed_counter > 0 ){
	    draw = true;
	    runCounter += Global::speed_counter * Global::LOGIC_MULTIPLIER;
	    while ( runCounter >= 1.0 ){
		runCounter -= 1;
		// Key handler
		InputManager::poll();
		
		InputMap<Mugen::Keys>::Output out = InputManager::getMap(gameInput);
		if (out[Mugen::Esc]){
		    done = escaped = true;
		    fader.setState(FADEOUT);
                    // play cancel sound
                    if (cancelSound){
                        cancelSound->play();
                    }
		    InputManager::waitForRelease(gameInput, Mugen::Esc);
		}
		
		/* *FIXME remove later when solution is found */
		if (checkPlayerData()){
		    done = true;
		    fader.setState(FADEOUT);
		}
		
		// Logic
		
		// Fader
		fader.act();
		
		// Backgrounds
		background->act();
		
		// Grid
		grid.act(player1Cursor,player2Cursor);
		
		// Cursors
		player1Cursor.act(grid);
		player2Cursor.act(grid);
		
		// Title
		titleFont.act();
	    }
	    
	    Global::speed_counter = 0;
	}
		
	while ( Global::second_counter > 0 ){
	    game_time--;
	    Global::second_counter--;
	    if ( game_time < 0 ){
		    game_time = 0;
	    }
	}

	if ( draw ){
	    // render backgrounds
	    background->renderBackground(0,0,workArea);
	    // Render Grid
	    grid.render(workArea);
	    // Render cursors
	    player1Cursor.render(grid, workArea);
	    player2Cursor.render(grid, workArea);
	    
	    // render title
	    titleFont.render(title,workArea);
	    
	    // render Foregrounds
	    background->renderForeground(0,0,workArea);
	    
	    // render fades
	    fader.draw(workArea);
	    
	    // Finally render to screen
	    workArea.Stretch(bmp);
	    bmp.BlitToScreen();
	}

	while ( Global::speed_counter < 1 ){
		PaintownUtil::rest( 1 );
	}
    }
    
    // **FIXME Hack figure something out
    if (escaped){
	throw ReturnException();
    }
}

void CharacterSelect::renderVersusScreen(const Bitmap & bmp){
    versus.render(*currentPlayer1,*currentPlayer2, currentStage, bmp);
}

bool CharacterSelect::setNextArcadeMatch(){
    if (arcadeMatches.empty()){
	return false;
    }
    std::queue<CharacterInfo *> & characters = arcadeMatches.front();
    if (characters.empty()){
	arcadeMatches.pop();
	if (arcadeMatches.empty()){
	    return false;
	}
	characters = arcadeMatches.front();
    }
    currentPlayer2 = characters.front();
    characters.pop();
    return true;
}

bool CharacterSelect::setNextTeamMatch(){
    return false;
}

bool CharacterSelect::checkPlayerData(){
    switch (type){
	case Arcade:
	    if (player1Cursor.getState() == Cursor::Done){
		currentPlayer1 = player1Cursor.getCurrentCell()->getCharacter();
		// Set initial player
		setNextArcadeMatch();
		if (currentStage){
		    delete currentStage;
		}
		if (currentPlayer2->hasRandomStage()){
		    currentStage = new MugenStage(grid.getStageHandler().getRandomStage());
		} else {
		    currentStage = new MugenStage(currentPlayer2->getStage());
		}
		return true;
	    }
	    break;
	case Versus:
	    if ((player1Cursor.getState() == Cursor::Done) && (player2Cursor.getState() == Cursor::Done)){
		currentPlayer1 = player1Cursor.getCurrentCell()->getCharacter();
		currentPlayer2 = player2Cursor.getCurrentCell()->getCharacter();
		if (currentStage){
		    delete currentStage;
		}
		currentStage = new MugenStage(grid.getStageHandler().getStage());
		return true;
	    }
	    break;
	case TeamArcade:
	    /* Ignore */
	    break;
	case TeamVersus:
	    /* Ignore */
	    break;
	case TeamCoop:
	    /* Ignore */
	    break;
	case Survival:
	    /* Ignore */
	    break;
	case SurvivalCoop:
	    /* Ignore */
	    break;
	case Training:
	    /* Ignore */
	    break;
	case Watch:
	    /* Ignore */
	    break;
	default:
	    /* Ignore */
	    break;
    }
    return false;
}
