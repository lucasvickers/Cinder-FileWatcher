#pragma once

#include "cinder/app/App.h"

#include <fstream>
#include <map>

#include "FileWatcher.h"

struct Action {
	int added;
	int modified;
	int removed;
	int renamedOld;
	int renamedNew;
	
	Action()
	: added( 0 ), modified( 0 ), removed( 0 ), renamedOld( 0 ), renamedNew( 0 )
	{ }
	
	void process( filewatcher::EventType type ) {
		switch( type ) {
			case filewatcher::EventType::ADDED:
				++added;
				break;
			case filewatcher::EventType::MODIFIED:
				++modified;
				break;
			case filewatcher::EventType::REMOVED:
				++removed;
				break;
			case filewatcher::EventType::RENAMED_OLD:
				++renamedOld;
				break;
			case filewatcher::EventType::RENAMED_NEW:
				++renamedNew;
				break;
			default:
				break;
		}
	}
};

typedef std::map<cinder::fs::path, Action> ActionMap;

inline cinder::fs::path getTestingPath()
{
	return cinder::app::getAppPath() / "logtest";
}

inline bool createTestingDir( const cinder::fs::path& path )
{
	if( ! cinder::fs::is_directory( path ) ) {
		return cinder::fs::create_directory( path );
	}
	return true;
}

inline void writeToFile( const cinder::fs::path &file, const std::string &text )
{
	std::ofstream ofile;
	ofile.open( file.string() );
	ofile << text;
	ofile.flush();
	ofile.close();
}

