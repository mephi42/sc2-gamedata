#include <stdio.h>
#include <tchar.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <map>
#include <cstdint>
#include <vector>
#include <exception>
#include <fstream>
#include <memory>
#include <set>
#include <streambuf>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>

#pragma warning(push)
#pragma warning(disable: 4275 4996)
#include <json/json.h>
#include <tinyxml2.h>
#pragma warning(pop)

using std::string;
using std::vector;
using std::set;
using std::exception;
using std::ofstream;
using std::ifstream;

using StringVector = vector<string>;
using StringSet = set<string>;

using NameToIDMapping = std::map<string, size_t>;

NameToIDMapping g_unitMapping;
NameToIDMapping g_abilityMapping;
NameToIDMapping g_upgradeMapping;

using AliasMap = std::map<string, set<string>>;

AliasMap g_aliases;

inline StringSet resolveAlias( string alias )
{
  if ( g_aliases.find( alias ) == g_aliases.end() )
  {
    StringSet set;
    set.insert( alias );
    return set;
  }
  return g_aliases[alias];
}

enum Race {
  Race_Neutral,
  Race_Terran,
  Race_Protoss,
  Race_Zerg
};

enum ResourceType {
  Resource_None,
  Resource_Minerals,
  Resource_Vespene,
  Resource_Terrazine,
  Resource_Custom
};

struct UnitAbilityCard {
  string name;
  std::map<uint64_t, string> commands;
  bool removed;
  size_t indexCtr;
  UnitAbilityCard(): removed( false ), indexCtr( 0 ) {}
};

struct Unit {
  string name;
  Race race;
  int64_t lifeStart;
  int64_t lifeMax;
  double speed;
  double acceleration;
  int64_t food;
  bool light;
  bool biological;
  bool mechanical;
  bool armored;
  bool structure;
  bool psionic;
  bool massive;
  int64_t sight;
  int64_t cargoSize;
  double turningRate;
  int64_t shieldsStart;
  int64_t shieldsMax;
  double lifeRegenRate;
  double radius;
  int64_t lifeArmor;
  double speedMultiplierCreep;
  int64_t mineralCost;
  int64_t vespeneCost;
  bool campaign;
  // StringSet abilityCommands;
  //std::map<uint64_t, string> abilityCommandsMap;
  std::map<size_t, UnitAbilityCard> abilityCardsMap;
  string mover;
  int64_t shieldRegenDelay;
  int64_t shieldRegenRate;
  StringSet weapons;
  int64_t scoreMake;
  int64_t scoreKill;
  ResourceType resourceType;
  bool invulnerable;
  bool resourceHarvestable; // vs. Raw (geyser without extractor)
  string footprint;
  Unit(): race( Race_Neutral ), lifeStart( 0 ), lifeMax( 0 ), speed( 0.0 ), acceleration( 0.0 ), food( 0 ),
    light( false ), biological( false ), mechanical( false ), armored( false ), structure( false ), psionic( false ), massive( false ),
    sight( 0 ), cargoSize( 0 ), turningRate( 0.0 ), shieldsStart( 0 ), shieldsMax( 0 ), lifeRegenRate( 0.0 ), radius( 0 ), lifeArmor( 0 ),
    speedMultiplierCreep( 1.0 ), mineralCost( 0 ), vespeneCost( 0 ), shieldRegenDelay( 0 ), shieldRegenRate( 0 ),
    scoreMake( 0 ), scoreKill( 0 ), resourceType( Resource_None ), invulnerable( false ), resourceHarvestable( false ), campaign( false )
  {
  }
};

struct Upgrade {
  string name;
  Race race;
};

using UpgradeMap = std::map<string, Upgrade>;

enum AbilType {
  AbilType_Train,
  AbilType_Morph,
  AbilType_Build,
  AbilType_Merge, // archon
  AbilType_Research,
  AbilType_Other
};

struct AbilityCommand {
  string index;
  double time;
  StringVector units;
  string requirements;
  bool isUpgrade;
  string upgrade; // upgrade name
  int64_t mineralCost; // for upgrade
  int64_t vespeneCost; // for upgrade
  AbilityCommand( const string& idx ): index( idx ), time( 0.0 ), mineralCost( 0 ), vespeneCost( 0 ), isUpgrade( false ) {}
  AbilityCommand(): time( 0.0 ) {}
};

using AbilityCommandMap = std::map<string, AbilityCommand>;

struct Ability {
  string name;
  AbilType type;
  AbilityCommandMap commands;
  string morphUnit;
  bool warp;
  Ability(): type( AbilType_Other ), warp( false ) {}
};

using AbilityMap = std::map<string, Ability>;

using UnitMap = std::map<string, Unit>;
using UnitVector = std::vector<Unit>;

inline Race raceToEnum( const char* str )
{
  if ( _strcmpi( str, "Terr" ) == 0 )
    return Race_Terran;
  else if ( _strcmpi( str, "Prot" ) == 0 )
    return Race_Protoss;
  else if ( _strcmpi( str, "Zerg" ) == 0 )
    return Race_Zerg;
  else
    return Race_Neutral;
}

inline bool boolValue( tinyxml2::XMLElement* element )
{
  return ( element->IntAttribute( "value" ) > 0 ? true : false );
}

inline ResourceType resourceToEnum( const char* str )
{
  if ( _strcmpi( str, "Minerals" ) == 0 )
    return Resource_Minerals;
  else if ( _strcmpi( str, "Vespene" ) == 0 )
    return Resource_Vespene;
  else if ( _strcmpi( str, "Terrazine" ) == 0 )
    return Resource_Terrazine;
  else if ( _strcmpi( str, "Custom" ) == 0 )
    return Resource_Custom;
  else
    return Resource_None;
}

inline uint64_t encodePoint( uint64_t page, uint64_t index )
{
  return ( page << 32 ) | ( index );
};

void parseUnitData( const string& filename, UnitMap& units, size_t& notFoundCount )
{
  notFoundCount = 0;

  tinyxml2::XMLDocument doc;
  if ( doc.LoadFile( filename.c_str() ) != tinyxml2::XML_SUCCESS )
    throw exception( "Could not load XML file" );

  auto catalog = doc.FirstChildElement( "Catalog" );
  auto entry = catalog->FirstChildElement( "CUnit" );
  while ( entry )
  {
    auto id = entry->Attribute( "id" );
    if ( id )
    {
      if ( entry->Attribute( "parent" ) && strlen( entry->Attribute( "parent" ) ) > 0 )
      {
        string parentId = entry->Attribute( "parent" );
        if ( units.find( parentId ) == units.end() )
        {
          printf( "Skipping unit %s because it has a parent that has not been parsed yet\r\n", id );
          notFoundCount++;
          entry = entry->NextSiblingElement( "CUnit" );
          continue;
        }
        else if ( units.find( id ) == units.end() )
        {
          // copy base data from parent before parsing this descendant
          // if it does not exist yet
          printf( "Copying unit %s from parent %s because it does not exist yet\r\n", id, parentId.c_str() );
          const Unit& parent = units[parentId];
          units[id] = parent;
        }
      }

      Unit& unit = units[id];
      unit.name = id;

      printf_s( "[+] unit: %s\r\n", unit.name.c_str() );

      size_t cardctr = 0; // index of current CardLayouts subitem
      auto field = entry->FirstChildElement();
      while ( field )
      {
        if ( _strcmpi( field->Name(), "Race" ) == 0 )
          unit.race = raceToEnum( field->Attribute( "value" ) );
        else if ( _stricmp( field->Name(), "LifeStart" ) == 0 )
          unit.lifeStart = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "LifeMax" ) == 0 )
          unit.lifeMax = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "Speed" ) == 0 )
          unit.speed = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "Acceleration" ) == 0 )
          unit.acceleration = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "Food" ) == 0 )
          unit.food = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "Sight" ) == 0 )
          unit.sight = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "ScoreMake" ) == 0 )
          unit.scoreMake = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "ScoreKill" ) == 0 )
          unit.scoreKill = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "Attributes" ) == 0 )
        {
          if ( _stricmp( field->Attribute( "index" ), "Light" ) == 0 )
            unit.light = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Biological" ) == 0 )
            unit.biological = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Mechanical" ) == 0 )
            unit.mechanical = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Armored" ) == 0 )
            unit.armored = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Structure" ) == 0 )
            unit.structure = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Psionic" ) == 0 )
            unit.psionic = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Massive" ) == 0 )
            unit.massive = boolValue( field );
        }
        else if ( _stricmp( field->Name(), "ResourceType" ) == 0 )
          unit.resourceType = resourceToEnum( field->Attribute( "value" ) );
        else if ( _stricmp( field->Name(), "ResourceState" ) == 0 && field->Attribute( "value" ) )
          unit.resourceHarvestable = ( _stricmp( field->Attribute( "value" ), "Harvestable" ) == 0 );
        else if ( _stricmp( field->Name(), "CargoSize" ) == 0 )
          unit.cargoSize = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "ShieldsStart" ) == 0 )
          unit.shieldsStart = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "ShieldsMax" ) == 0 )
          unit.shieldsMax = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "TurningRate" ) == 0 )
          unit.turningRate = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "LifeRegenRate" ) == 0 )
          unit.lifeRegenRate = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "Radius" ) == 0 )
          unit.radius = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "LifeArmor" ) == 0 )
          unit.lifeArmor = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "SpeedMultiplierCreep" ) == 0 )
          unit.speedMultiplierCreep = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "CostResource" ) == 0 )
        {
          if ( _stricmp( field->Attribute( "index" ), "Minerals" ) == 0 )
            unit.mineralCost = field->Int64Attribute( "value" );
          else if ( _stricmp( field->Attribute( "index" ), "Vespene" ) == 0 )
            unit.vespeneCost = field->Int64Attribute( "value" );
        }
        else if ( _stricmp( field->Name(), "CardLayouts" ) == 0 )
        {
          size_t cardidx = ( field->Attribute( "index" ) ? field->UnsignedAttribute( "index" ) : cardctr );
          auto& card = unit.abilityCardsMap[cardidx];
          if ( field->Attribute( "CardId" ) )
            card.name = field->Attribute( "CardId" );
          if ( field->Attribute( "removed" ) && field->Int64Attribute( "removed" ) > 0 )
            card.removed = true;
          else
            card.removed = false;
          auto sub = field->FirstChildElement( "LayoutButtons" );
          size_t ctr = card.indexCtr; // ctr = 0;
          while ( sub )
          {
            ctr = ( sub->Attribute( "index" ) ? sub->Int64Attribute( "index" ) : ctr );
            if ( ( sub->Attribute( "removed" ) && sub->Int64Attribute( "removed" ) > 0 ) || ( sub->Attribute( "Type" ) && _stricmp( sub->Attribute( "Type" ), "Undefined" ) == 0 ) )
              card.commands.erase( ctr );
            else if ( sub->Attribute( "Type" ) && _stricmp( sub->Attribute( "Type" ), "AbilCmd" ) == 0 && sub->Attribute( "AbilCmd" ) )
              card.commands[ctr] = sub->Attribute( "AbilCmd" );

            sub = sub->NextSiblingElement( "LayoutButtons" );
            ctr++;
          }
          card.indexCtr = ctr;
          cardctr++;
        }
        else if ( _strcmpi( field->Name(), "TechAliasArray" ) == 0 && field->Attribute( "value" ) )
          g_aliases[field->Attribute( "value" )].insert( unit.name );
        else if ( _stricmp( field->Name(), "Mover" ) == 0 && field->Attribute( "value" ) )
          unit.mover = field->Attribute( "value" );
        else if ( _stricmp( field->Name(), "ShieldRegenDelay" ) == 0 && field->Attribute( "value" ) )
          unit.shieldRegenDelay = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "ShieldRegenRate" ) == 0 && field->Attribute( "value" ) )
          unit.shieldRegenRate = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "WeaponArray" ) == 0 && field->Attribute( "Link" ) )
          unit.weapons.insert( field->Attribute( "Link" ) );
        else if ( _strcmpi( field->Name(), "FlagArray" ) == 0 && field->Attribute( "index" ) && field->Attribute( "value" ) )
        {
          if ( _stricmp( field->Attribute( "index" ), "Invulnerable" ) == 0 )
            unit.invulnerable = ( field->IntAttribute( "value" ) > 0 ? true : false );
        }
        else if ( _stricmp( field->Name(), "Footprint" ) == 0 && field->Attribute( "value" ) )
          unit.footprint = field->Attribute( "value" );
        else if ( _stricmp( field->Name(), "EditorCategories" ) == 0 && field->Attribute( "value" ) )
        {
          string editorCategories = field->Attribute( "value" );
          if ( editorCategories.find( "ObjectFamily:Campaign" ) != string::npos )
            unit.campaign = true;
        }
        /*else if ( _stricmp( field->Name(), "AbilArray" ) == 0 && field->Attribute( "Link" ) )
          unit.abilities.insert( field->Attribute( "Link" ) );*/

        field = field->NextSiblingElement();
      }
    }
    entry = entry->NextSiblingElement( "CUnit" );
  }
}

struct Requirement {
  string id;
  string useNodeName;
  string showNodeName;
  Requirement( const string& id_ = "" ): id( id_ ) {}
};

using RequirementMap = std::map<string, Requirement>;

enum RequirementNodeType {
  ReqNode_Unknown,
  ReqNode_CountUpgrade,
  ReqNode_CountUnit,
  ReqNode_LogicAnd,
  ReqNode_LogicOr,
  ReqNode_LogicEq,
  ReqNode_LogicNot
};

inline RequirementNodeType reqNodeTypeToEnum( const char* str )
{
  if ( _strcmpi( str, "CRequirementCountUpgrade" ) == 0 )
    return ReqNode_CountUpgrade;
  else if ( _strcmpi( str, "CRequirementCountUnit" ) == 0 )
    return ReqNode_CountUnit;
  else if ( _strcmpi( str, "CRequirementAnd" ) == 0 )
    return ReqNode_LogicAnd;
  else if ( _strcmpi( str, "CRequirementOr" ) == 0 )
    return ReqNode_LogicOr;
  else if ( _strcmpi( str, "CRequirementEq" ) == 0 )
    return ReqNode_LogicEq;
  else if ( _strcmpi( str, "CRequirementNot" ) == 0 )
    return ReqNode_LogicNot;
  else
    return ReqNode_Unknown;
}

struct RequirementNode {
  string id;
  RequirementNodeType type;
  string countLink; // countUpgrade, countUnit
  string countState; // countUpgrade, countUnit
  std::map<size_t, string> operands; // and, or, eq, not
  RequirementNode( const string& id_ = "" ): id( id_ ), type( ReqNode_Unknown ) {}
};

using RequirementNodeMap = std::map<string, RequirementNode>;

void parseRequirementData( const string& datafilename, const string& nodedatafilename, RequirementMap& requirements, RequirementNodeMap& nodes )
{
  tinyxml2::XMLDocument datadoc;
  if ( datadoc.LoadFile( datafilename.c_str() ) != tinyxml2::XML_SUCCESS )
    throw exception( "Could not load RequirementData XML file" );

  tinyxml2::XMLDocument nodedatadoc;
  if ( nodedatadoc.LoadFile( nodedatafilename.c_str() ) != tinyxml2::XML_SUCCESS )
    throw exception( "Could not load RequirementNodeData XML file" );

  // RequirementData.xml
  auto catalog = datadoc.FirstChildElement( "Catalog" );
  auto entry = catalog->FirstChildElement( "CRequirement" );
  while ( entry )
  {
    auto id = entry->Attribute( "id" );
    if ( id )
    {
      if ( requirements.find( id ) == requirements.end() )
        requirements[id] = Requirement( id );

      Requirement& requirement = requirements[id];
      auto child = entry->FirstChildElement();
      while ( child )
      {
        if ( _stricmp( child->Name(), "NodeArray" ) == 0 && child->Attribute( "index" ) && child->Attribute( "Link" ) )
        {
          if ( _stricmp( child->Attribute( "index" ), "Use" ) == 0 )
            requirement.useNodeName = child->Attribute( "Link" );
          if ( _stricmp( child->Attribute( "index" ), "Show" ) == 0 )
            requirement.showNodeName = child->Attribute( "Link" );
        }
        child = child->NextSiblingElement();
      }
    }
    entry = entry->NextSiblingElement( "CRequirement" );
  }

  // RequirementNodeData.xml
  catalog = nodedatadoc.FirstChildElement( "Catalog" );
  entry = catalog->FirstChildElement();
  while ( entry )
  {
    auto id = entry->Attribute( "id" );
    if ( id )
    {
      if ( nodes.find( id ) == nodes.end() )
        nodes[id] = RequirementNode( id );

      RequirementNode& node = nodes[id];
      node.type = reqNodeTypeToEnum( entry->Name() );
      if ( node.type == ReqNode_LogicAnd || node.type == ReqNode_LogicOr || node.type == ReqNode_LogicEq || node.type == ReqNode_LogicNot )
        node.operands.clear();

      auto child = entry->FirstChildElement();
      while ( child )
      {
        if ( _stricmp( child->Name(), "Count" ) == 0 )
        {
          if ( child->Attribute( "Link" ) )
            node.countLink = child->Attribute( "Link" );
          if ( child->Attribute( "State" ) )
            node.countState = child->Attribute( "State" );
        }
        else if ( _stricmp( child->Name(), "OperandArray" ) == 0 && child->Attribute( "value" ) )
        {
          size_t opIndex = child->UnsignedAttribute( "index", node.operands.size() );
          node.operands[opIndex] = child->Attribute( "value" );
        }
        child = child->NextSiblingElement();
      }
    }
    entry = entry->NextSiblingElement();
  }
}

struct Footprint {
  string id;
  string parent;
  int x;
  int y;
  int w;
  int h;
  bool removed;
  vector<char> placement;
  Footprint(): x( 0 ), y( 0 ), w( 0 ), h( 0 ), removed( false ) {}
};

using FootprintMap = std::map<string, Footprint>;

void parseFootprintData( const string& filename, FootprintMap& footprints )
{
  tinyxml2::XMLDocument doc;
  if ( doc.LoadFile( filename.c_str() ) != tinyxml2::XML_SUCCESS )
    throw exception( "Could not load FootprintData XML file" );

  // FootprintData.xml
  auto catalog = doc.FirstChildElement( "Catalog" );
  auto entry = catalog->FirstChildElement( "CFootprint" );
  while ( entry )
  {
    auto id = entry->Attribute( "id" );
    if ( id )
    {
      Footprint& fp = footprints[id];
      fp.id = id;

      if ( entry->Attribute( "parent" ) )
        fp.parent = entry->Attribute( "parent" );

      auto layer = entry->FirstChildElement( "Layers" );
      while ( layer )
      {
        if ( layer->Attribute( "index" ) && boost::iequals( layer->Attribute( "index" ), "Place" ) )
        {
          if ( layer->IntAttribute( "removed", 0 ) == 1 )
          {
            fp.removed = true;
            layer = layer->NextSiblingElement( "Layers" );
            continue;
          }

          fp.removed = false;

          if ( layer->Attribute( "Area" ) )
          {
            string area = layer->Attribute( "Area" );
            vector<string> parts;
            boost::split( parts, area, boost::is_any_of( "," ) );
            if ( parts.size() != 4 )
              throw exception( "bad Area attribute for CFootprint::Layers" );

            fp.x = atoi( parts[0].c_str() );
            fp.y = atoi( parts[1].c_str() );
            int rx = atoi( parts[2].c_str() );
            int ry = atoi( parts[3].c_str() );
            fp.w = ( rx - fp.x );
            fp.h = ( ry - fp.y );
          }

          if ( layer->FirstChildElement( "Rows" ) && ( !layer->FirstChildElement( "Rows" )->Attribute( "removed" ) || layer->FirstChildElement( "Rows" )->IntAttribute( "removed" ) == 0 ) )
          {
            // jesus christ
            int rowcount = 0;
            int rowwidth = 0;
            auto ctr = layer->FirstChildElement( "Rows" );
            while ( ctr )
            {
              size_t val = ( ctr->Attribute( "value" ) ? strlen( ctr->Attribute( "value" ) ) : 0 );
              if ( val > rowwidth )
                rowwidth = val;
              rowcount++;
              ctr = ctr->NextSiblingElement( "Rows" );
            }

            if ( rowcount > fp.h )
              fp.h = rowcount;
            if ( rowwidth > fp.w )
              fp.w = rowwidth;

            fp.placement.clear();
            fp.placement.resize( fp.w * fp.h, 0 );
            size_t index = 0;
            auto row = layer->FirstChildElement( "Rows" );
            while ( row )
            {
              if ( !row->Attribute( "value" ) )
                throw exception( "CFootprint::Layers::Rows without value attribute" );
              string value = row->Attribute( "value" );
              if ( value.length() > fp.w )
                fp.w = value.length();
              for ( size_t c = 0; c < value.length(); c++ )
                if ( value[c] == 'x' )
                  fp.placement[index * fp.w + c] = 1;
              index++;
              row = row->NextSiblingElement( "Rows" );
            }
          }
        }
        layer = layer->NextSiblingElement( "Layers" );
      }
    }
    entry = entry->NextSiblingElement( "CFootprint" );
  }
}

void parseAbilityData( const string& filename, AbilityMap& abilities )
{
  string abilstr;

  ifstream in;
  in.open( filename, std::ifstream::in );
  if ( !in.is_open() )
    throw exception( "Could not load XML file" );
  in.seekg( 0, std::ios::end );
  abilstr.reserve( in.tellg() );
  in.seekg( 0, std::ios::beg );
  abilstr.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
  in.close();

  boost::replace_all( abilstr, R"(<?token id="unit"?>)", "" );

  tinyxml2::XMLDocument doc;
  auto ret = doc.Parse( abilstr.c_str() );
  if ( ret != tinyxml2::XML_SUCCESS )
  {
    doc.PrintError();
    throw exception( "Could not parse XML file" );
  }

  auto catalog = doc.FirstChildElement( "Catalog" );
  auto entry = catalog->FirstChildElement();
  while ( entry )
  {
    auto id = entry->Attribute( "id" );
    if ( id )
    {
      Ability& abil = abilities[id];
      abil.name = id;
      if ( _stricmp( entry->Name(), "CAbilTrain" ) == 0 )
        abil.type = AbilType_Train;
      else if ( _stricmp( entry->Name(), "CAbilWarpTrain" ) == 0 )
      {
        abil.type = AbilType_Train;
        abil.warp = true;
      }
      else if ( _stricmp( entry->Name(), "CAbilMorph" ) == 0 )
        abil.type = AbilType_Morph;
      else if ( _stricmp( entry->Name(), "CAbilBuild" ) == 0 )
        abil.type = AbilType_Build;
      else if ( _stricmp( entry->Name(), "CAbilMerge" ) == 0 )
        abil.type = AbilType_Merge;
      else if ( _stricmp( entry->Name(), "CAbilResearch" ) == 0 )
        abil.type = AbilType_Research;

      printf_s( "[+] ability: %s\r\n", abil.name.c_str() );

      auto field = entry->FirstChildElement();
      while ( field )
      {
        if ( _strcmpi( field->Name(), "MorphUnit" ) == 0 )
          abil.morphUnit = field->Attribute( "value" );
        else if ( _strcmpi( field->Name(), "InfoArray" ) == 0 )
        {
          auto idx = field->Attribute( "index" );
          if ( ( abil.type == AbilType_Train || abil.type == AbilType_Build ) && idx ) // for CAbilTrain & CAbilBuild
          {
            if ( abil.commands.find( idx ) == abil.commands.end() )
              abil.commands[idx] = AbilityCommand( idx );

            AbilityCommand& cmd = abil.commands[idx];
            if ( field->Attribute( "Time" ) )
              cmd.time = field->DoubleAttribute( "Time" );

            auto unit = field->Attribute( "Unit" );
            if ( unit )
              cmd.units.emplace_back( unit );

            if ( field->FirstChildElement( "Unit" ) )
              cmd.units.clear();
            auto sub = field->FirstChildElement();
            while ( sub )
            {
              if ( _strcmpi( sub->Name(), "Unit" ) == 0 && sub->Attribute( "value" ) )
                cmd.units.emplace_back( sub->Attribute( "value" ) );
              else if ( _strcmpi( sub->Name(), "Button" ) == 0 && sub->Attribute( "Requirements" ) )
                cmd.requirements = sub->Attribute( "Requirements" );
              sub = sub->NextSiblingElement();
            }
          }
          else if ( abil.type == AbilType_Morph && field->Attribute( "Unit" ) ) // for CAbilMorph
          {
            // Note: current implementation misses CAbilMorphs and others that descend from a "parent" (attribute in AbilData.xml)
            // this includes at least TerranBuildingLiftOff, DisguiseChangeling and such
            if ( abil.commands.find( "Execute" ) == abil.commands.end() )
              abil.commands["Execute"] = AbilityCommand( "Execute" );

            AbilityCommand& cmd = abil.commands["Execute"];
            cmd.units.emplace_back( field->Attribute( "Unit" ) );

            auto sect = field->FirstChildElement( "SectionArray" );
            while ( sect )
            {
              if ( sect->Attribute( "index" ) && _stricmp( sect->Attribute( "index" ), "Actor" ) == 0 )
              {
                auto dur = sect->FirstChildElement( "DurationArray" );
                if ( dur && dur->Attribute( "index" ) && _stricmp( dur->Attribute( "index" ), "Delay" ) == 0 && dur->Attribute( "value" ) )
                  cmd.time = dur->DoubleAttribute( "value" );
              }
              sect = sect->NextSiblingElement( "SectionArray" );
            }
          }
          else if ( abil.type == AbilType_Research && field->Attribute( "Upgrade" ) ) // for CAbilResearch
          {
            if ( strlen( field->Attribute( "Upgrade" ) ) < 1 )
            {
              auto it = abil.commands.find( idx );
              if ( it != abil.commands.end() )
                abil.commands.erase( it );
            }
            else
            {
              if ( abil.commands.find( idx ) == abil.commands.end() )
                abil.commands[idx] = AbilityCommand( idx );

              AbilityCommand& cmd = abil.commands[idx];
              if ( field->Attribute( "Time" ) )
                cmd.time = field->DoubleAttribute( "Time" );

              cmd.isUpgrade = true;
              cmd.upgrade = field->Attribute( "Upgrade" );
              auto sub = field->FirstChildElement();
              while ( sub )
              {
                if ( _strcmpi( sub->Name(), "Resource" ) == 0 && sub->Attribute( "index" ) && _stricmp( sub->Attribute( "index" ), "Minerals" ) == 0 && sub->Attribute( "value" ) )
                  cmd.mineralCost = sub->Int64Attribute( "value" );
                else if ( _strcmpi( sub->Name(), "Resource" ) == 0 && sub->Attribute( "index" ) && _stricmp( sub->Attribute( "index" ), "Vespene" ) == 0 && sub->Attribute( "value" ) )
                  cmd.vespeneCost = sub->Int64Attribute( "value" );
                else if ( _strcmpi( sub->Name(), "Button" ) == 0 && sub->Attribute( "Requirements" ) )
                  cmd.requirements = sub->Attribute( "Requirements" );
                sub = sub->NextSiblingElement();
              }
            }
          }
        }
        else if ( abil.type == AbilType_Morph &&  _strcmpi( field->Name(), "CmdButtonArray" ) == 0 && field->Attribute( "index" ) && _stricmp( field->Attribute( "index" ), "Execute" ) == 0 && field->Attribute( "Requirements" ) )
        {
          if ( abil.commands.find( "Execute" ) == abil.commands.end() )
            abil.commands["Execute"] = AbilityCommand( "Execute" );
          abil.commands["Execute"].requirements = field->Attribute( "Requirements" );
        }

        // TODO i'm tired to death and don't care about ArchonWarp right now for this PoC
        /*else if ( abil.type == AbilType_Merge && _strcmpi( field->Name(), "Info" ) == 0 )
        {
          if ( field->Attribute( "Unit" ) )
            abil.morphUnit = field->Attribute( "Unit" );
          if ( field->Attribute( "Time" ) )
            abil.
        }*/
        /*
             <CAbilMerge id="ArchonWarp">
        <EditorCategories value="Race:Protoss,AbilityorEffectType:Units"/>
        <CmdButtonArray index="SelectedUnits" DefaultButtonFace="AWrp" State="Restricted">
            <Flags index="ToSelection" value="1"/>
        </CmdButtonArray>
        <CmdButtonArray index="WithTarget" DefaultButtonFace="ArchonWarpTarget" State="Restricted"/>
        <Info Unit="Archon" Time="16.6667">
            <Resource index="Minerals" value="-175"/>
            <Resource index="Vespene" value="-275"/>
        </Info>
    </CAbilMerge>
         */
        field = field->NextSiblingElement();
      }
    }
    entry = entry->NextSiblingElement();
  }
}

const char* raceStr( Race race )
{
  if ( race == Race_Terran )
    return "terran";
  else if ( race == Race_Zerg )
    return "zerg";
  else if ( race == Race_Protoss )
    return "protoss";
  else
    return "neutral";
}

const char* resourceStr( ResourceType type )
{
  if ( type == Resource_Minerals )
    return "minerals";
  else if ( type == Resource_Vespene )
    return "vespene";
  else if ( type == Resource_Terrazine )
    return "terrazine";
  else if ( type == Resource_Custom )
    return "custom";
  else
    return "";
}

const char* abilTypeStr( AbilType type )
{
  if ( type == AbilType_Build )
    return "build";
  else if ( type == AbilType_Morph )
    return "morph";
  else if ( type == AbilType_Train )
    return "train";
  else if ( type == AbilType_Research )
    return "research";
  else
    return "";
}

void resolveFootprint( const string& name, FootprintMap& footprints, Json::Value& out )
{
  if ( name.empty() || footprints.find( name ) == footprints.end() )
    return;

  auto& fp = footprints[name];

  // naively get parent if override offers nothing, works well enough
  if ( ( fp.w < 1 || fp.h < 1 ) && !fp.parent.empty() )
  {
    return resolveFootprint( fp.parent, footprints, out );
  }

  Json::Value offset( Json::arrayValue );
  offset.append( fp.x );
  offset.append( fp.y );
  out["offset"] = offset;

  Json::Value dim( Json::arrayValue );
  dim.append( fp.w );
  dim.append( fp.h );
  out["dimensions"] = dim;

  string data;
  for ( size_t y = 0; y < fp.h; y++ )
    for ( size_t x = 0; x < fp.w; x++ )
    {
      auto idx = y * fp.w + x;
      data.append( ( fp.placement[idx] ? "x" : "." ) );
    }
  out["data"] = data;
}

void dumpUnits( UnitMap& units, FootprintMap& footprints )
{
  printf_s( "[d] dumping units...\r\n" );

  ofstream out;
  out.open( "units.json" );

  Json::Value root;
  for ( auto& unit : units )
  {
    if ( unit.second.lifeStart == 0 && unit.second.lifeMax == 0 )
      continue;

    if ( unit.second.race == Race_Neutral )
      continue;

    // some cleanup
    if ( boost::iequals( unit.second.name.substr( 0, 7 ), "XelNaga" ) && !boost::iequals( unit.second.name, "XelNagaTower" ) )
      continue;
    if ( boost::iequals( unit.second.name.substr( 0, 4 ), "Aiur" )
      || boost::iequals( unit.second.name.substr( 0, 8 ), "PortCity" )
      || boost::iequals( unit.second.name.substr( 0, 8 ), "Shakuras" )
      || boost::iequals( unit.second.name.substr( 0, 15 ), "ExtendingBridge" ) )
      continue;

    Json::Value uval( Json::objectValue );
    uval["name"] = unit.second.name;

    uval["race"] = raceStr( unit.second.race );
    uval["food"] = unit.second.food;

    uval["mineralCost"] = unit.second.mineralCost;
    uval["vespeneCost"] = unit.second.vespeneCost;

    uval["speed"] = unit.second.speed;
    uval["acceleration"] = unit.second.acceleration;
    uval["speedMultiplierCreep"] = unit.second.speedMultiplierCreep;

    uval["radius"] = unit.second.radius;
    uval["sight"] = unit.second.sight;

    uval["lifeStart"] = unit.second.lifeStart;
    uval["lifeMax"] = unit.second.lifeMax;
    uval["lifeRegenRate"] = unit.second.lifeRegenRate;
    uval["lifeArmor"] = unit.second.lifeArmor;
    uval["shieldsStart"] = unit.second.shieldsStart;
    uval["shieldsMax"] = unit.second.shieldsMax;

    uval["light"] = unit.second.light;
    uval["biological"] = unit.second.biological;
    uval["mechanical"] = unit.second.mechanical;
    uval["armored"] = unit.second.armored;
    uval["structure"] = unit.second.structure;
    uval["psionic"] = unit.second.psionic;
    uval["massive"] = unit.second.massive;
    uval["cargoSize"] = unit.second.cargoSize;
    uval["turningRate"] = unit.second.turningRate;

    uval["shieldRegenDelay"] = unit.second.shieldRegenDelay;
    uval["shieldRegenRate"] = unit.second.shieldRegenRate;
    uval["mover"] = unit.second.mover;

    uval["scoreMake"] = unit.second.scoreMake;
    uval["scoreKill"] = unit.second.scoreKill;

    Json::Value fp( Json::objectValue );
    resolveFootprint( unit.second.footprint, footprints, fp );
    uval["footprint"] = fp;

    Json::Value resval( Json::arrayValue );
    if ( unit.second.resourceType != Resource_None )
    {
      resval.append( resourceStr( unit.second.resourceType ) );
      resval.append( unit.second.resourceHarvestable ? "harvestable" : "raw" );
    }
    uval["resource"] = resval;
    uval["invulnerable"] = unit.second.invulnerable;

    Json::Value weapons( Json::arrayValue );
    for ( auto& weapon : unit.second.weapons )
      weapons.append( weapon );
    uval["weapons"] = weapons;

    Json::Value abils( Json::arrayValue );
    //for ( auto& abil : unit.second.abilityCommands )
    //  abils.append( abil );
    for ( auto& card : unit.second.abilityCardsMap )
      if ( !card.second.removed )
        for ( auto& abil : card.second.commands )
          if ( !abil.second.empty() )
            abils.append( abil.second );

    uval["abilityCommands"] = abils;

    root[std::to_string( g_unitMapping[unit.second.name] )] = uval;
  }

  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "  ";
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  writer->write( root, &out );

  out.close();
}

bool resolveRequirements( const string& useNodeName, Json::Value& rqtmp, RequirementMap& requirements, RequirementNodeMap& nodes )
{
  if ( nodes.find( useNodeName ) == nodes.end() || useNodeName.size() < 5 ) // quick hack to identify numerals
  {
    rqtmp["type"] = "value";
    rqtmp["value"] = atoi( useNodeName.c_str() );
    return true;
  }
  auto& node = nodes[useNodeName];
  if ( node.type != ReqNode_Unknown )
  {
    if ( node.type == ReqNode_LogicAnd || node.type == ReqNode_LogicOr || node.type == ReqNode_LogicEq || node.type == ReqNode_LogicNot )
    {
      if ( node.operands.empty() )
        return false;
      rqtmp["type"] = ( node.type == ReqNode_LogicAnd ? "and" : node.type == ReqNode_LogicOr ? "or" : node.type == ReqNode_LogicEq ? "eq" : "not" );
      Json::Value rqops( Json::arrayValue );
      for ( auto& op : node.operands )
      {
        Json::Value optmp( Json::objectValue );
        if ( !resolveRequirements( op.second, optmp, requirements, nodes ) )
          continue;
        rqops.append( optmp );
      }
      rqtmp["operands"] = rqops;
    }
    else
    {
      rqtmp["type"] = ( node.type == ReqNode_CountUnit ? "unitCount" : node.type == ReqNode_CountUpgrade ? "upgradeCount" : "" );
      if ( node.type == ReqNode_CountUnit )
      {
        Json::Value namesVal( Json::arrayValue );
        Json::Value idsVal( Json::arrayValue );
        for ( auto& name : resolveAlias( node.countLink ) )
        {
          namesVal.append( name );
          idsVal.append( g_unitMapping[name] );
        }
        rqtmp["unitName"] = namesVal;
        rqtmp["unit"] = idsVal;
      }
      else if ( node.type == ReqNode_CountUpgrade )
      {
        rqtmp["upgradeName"] = node.countLink;
        rqtmp["upgrade"] = g_upgradeMapping[node.countLink];
      }
      if ( !node.countState.empty() )
        rqtmp["state"] = node.countState;
    }
    return true;
  }
  else
    return false;
}

size_t resolveAbilityCmd( const string& ability, const string& command )
{
  size_t cmdindex = 0;
  if ( boost::iequals( command, "Execute" ) ) // morphs
    cmdindex = 0;
  else if ( command.size() > 5 && boost::iequals( command.substr( 0, 5 ), "Build" ) ) // builds
  {
    string numpart = command.substr( 5 );
    cmdindex = ( atoi( numpart.c_str() ) - 1 );
  }
  else if ( command.size() > 5 && boost::iequals( command.substr( 0, 5 ), "Train" ) ) // trains
  {
    string numpart = command.substr( 5 );
    cmdindex = ( atoi( numpart.c_str() ) - 1 );
  }
  else if ( command.size() > 8 && boost::iequals( command.substr( 0, 8 ), "Research" ) ) // researches
  {
    string numpart = command.substr( 8 );
    cmdindex = ( atoi( numpart.c_str() ) - 1 );
  }
  string idx = ( ability + "," );
  idx.append( std::to_string( cmdindex ) );

  return g_abilityMapping[idx];
}

void dumpAbilities( AbilityMap& abils, RequirementMap& requirements, RequirementNodeMap& nodes )
{
  printf_s( "[d] dumping abilities...\r\n" );

  ofstream out;
  out.open( "abilities.json" );

  Json::Value root;
  for ( auto& abil : abils )
  {
    if ( abil.second.type == AbilType_Other )
      continue;

    Json::Value uval( Json::objectValue );
    uval["name"] = abil.second.name;
    uval["type"] = abilTypeStr( abil.second.type );
    if ( !abil.second.morphUnit.empty() )
      uval["morphUnit"] = abil.second.morphUnit;

    Json::Value cmds( Json::objectValue );
    for ( auto& cmd : abil.second.commands )
    {
      if ( abil.second.type == AbilType_Train && cmd.second.units.empty() )
        continue;

      Json::Value cval;
      cval["index"] = resolveAbilityCmd( abil.second.name, cmd.second.index );
      cval["time"] = cmd.second.time;
      if ( !cmd.second.requirements.empty() )
      {
        Json::Value reqsnode( Json::arrayValue );
        vector<string> rqs;
        if ( requirements.find( cmd.second.requirements ) != requirements.end() )
        {
          if ( !requirements[cmd.second.requirements].useNodeName.empty() )
            rqs.push_back( requirements[cmd.second.requirements].useNodeName );
          if ( !requirements[cmd.second.requirements].showNodeName.empty() )
            rqs.push_back( requirements[cmd.second.requirements].showNodeName );
        }
        else
          rqs.push_back( cmd.second.requirements );
        for ( auto& r : rqs )
        {
          Json::Value optmp( Json::objectValue );
          if ( resolveRequirements( r, optmp, requirements, nodes ) )
            reqsnode.append( optmp );
        }
        cval["requires"] = reqsnode;
      }

      if ( !cmd.second.units.empty() )
      {
        Json::Value units( Json::arrayValue );
        for ( auto& unit : cmd.second.units )
          if ( !unit.empty() )
            units.append( unit );

        cval["units"] = units;
      }

      cmds[cmd.second.index] = cval;
    }

    uval["commands"] = cmds;

    root[abil.second.name] = uval;
  }

  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "  ";
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  writer->write( root, &out );

  out.close();
}

struct TechTreeBuildEntry {
  string unit;
  string ability;
  string command;
  double time;
  string requirements;
};

struct TechTreeResearchEntry {
  string upgrade;
  string ability;
  string command;
  double time;
  string requirements;
  int64_t minerals;
  int64_t vespene;
};

struct TechTreeEntry {
  string id;
  vector<TechTreeBuildEntry> builds;
  vector<TechTreeBuildEntry> morphs;
  vector<TechTreeResearchEntry> researches;
};

using TechTree = vector<TechTreeEntry>;

using TechMap = std::map<Race, TechTree>;

void generateTechTree( UnitMap& units, AbilityMap& abilities, Race race, TechTree& tree )
{
  UnitMap buildings;
  for ( auto& unit : units )
  {
    if ( unit.second.race != race )
      continue;

    // skip campaign units
    // EDIT: except not, because widowmine is classified as a campaign unit (???)
    /*if ( unit.second.campaign )
      continue;*/

    // some cleanup
    if ( boost::iequals( unit.second.name.substr( 0, 7 ), "XelNaga" ) && !boost::iequals( unit.second.name, "XelNagaTower" ) )
      continue;
    if ( boost::iequals( unit.second.name.substr( 0, 4 ), "Aiur" )
      || boost::iequals( unit.second.name.substr( 0, 8 ), "PortCity" )
      || boost::iequals( unit.second.name.substr( 0, 8 ), "Shakuras" )
      || boost::iequals( unit.second.name.substr( 0, 15 ), "ExtendingBridge" ) )
      continue;

    TechTreeEntry entry;
    entry.id = unit.second.name;

    // for ( auto& name : unit.second.abilityCommands )
    for ( auto& card : unit.second.abilityCardsMap )
      if ( !card.second.removed )
        for ( auto& cmdpair : card.second.commands )
        {
          if ( cmdpair.second.empty() )
            continue;

          string name = cmdpair.second;
          // "Ability,Command"
          vector<string> parts;
          boost::split( parts, name, boost::is_any_of( "," ) );
          if ( parts.size() != 2 )
            continue;

          auto& ability = abilities[parts[0]];
          if ( ability.type == AbilType_Train || ability.type == AbilType_Build || ability.type == AbilType_Morph )
          {
            auto& cmd = ability.commands[parts[1]];
            // for ( auto& cmd : ability.commands )
            // {
            if ( cmd.units.empty() || cmd.isUpgrade )
              continue;

            TechTreeBuildEntry bentry;
            // training zergling has ["zergling", "zergling"] for example, so this simplification seems reasonable
            // but skip cocoons and eggs, we don't care
            for ( auto& u : cmd.units )
            {
              if ( u.find( "Cocoon" ) != std::string::npos )
                continue;
              if ( u.find( "Egg" ) != std::string::npos )
                continue;
              bentry.unit = u;
              break;
            }
            if ( bentry.unit.empty() )
              bentry.unit = cmd.units[cmd.units.size() - 1]; // else just choose the last one which is usually better than first
            bentry.ability = ability.name;
            bentry.time = cmd.time;
            bentry.command = cmd.index;
            bentry.requirements = cmd.requirements;

            // separate morphs from other types as morph consumes existing unit, others do not
            if ( ability.type == AbilType_Morph )
              entry.morphs.push_back( bentry );
            else
              entry.builds.push_back( bentry );
            // }
          }
          else if ( ability.type == AbilType_Research )
          {
            auto& cmd = ability.commands[parts[1]];
            if ( !cmd.isUpgrade )
              continue;

            TechTreeResearchEntry res;
            res.upgrade = cmd.upgrade;
            res.ability = ability.name;
            res.time = cmd.time;
            res.command = cmd.index;
            res.requirements = cmd.requirements;
            res.minerals = cmd.mineralCost;
            res.vespene = cmd.vespeneCost;
            entry.researches.push_back( res );
          }
        }

    tree.push_back( entry );

    buildings[unit.second.name] = unit.second;
  }
}

void dumpRequirementsJSON( const string& reqstr, RequirementMap& requirements, RequirementNodeMap& nodes, Json::Value& out )
{
  if ( !reqstr.empty() )
  {
    Json::Value reqsnode( Json::arrayValue );
    vector<string> rqs;
    if ( requirements.find( reqstr ) != requirements.end() )
    {
      if ( !requirements[reqstr].useNodeName.empty() )
        rqs.push_back( requirements[reqstr].useNodeName );
      if ( !requirements[reqstr].showNodeName.empty() )
        rqs.push_back( requirements[reqstr].showNodeName );
    }
    else
      rqs.push_back( reqstr );
    for ( auto& r : rqs )
    {
      Json::Value optmp( Json::objectValue );
      if ( resolveRequirements( r, optmp, requirements, nodes ) )
        reqsnode.append( optmp );
    }
    out["requires"] = reqsnode;
  }
}

void dumpTechTree( TechMap& techtree, RequirementMap& requirements, RequirementNodeMap& nodes )
{
  printf_s( "[d] dumping tech tree...\r\n" );

  ofstream out;
  out.open( "techtree.json" );

  Json::Value root;
  for ( auto& entry : techtree )
  {
    Json::Value racenode( Json::objectValue );
    for ( auto& asd : entry.second )
    {
      if ( asd.builds.empty() && asd.morphs.empty() && asd.researches.empty() )
        continue;

      Json::Value unitnode( Json::objectValue );
      unitnode["name"] = asd.id;

      if ( !asd.builds.empty() )
      {
        Json::Value buildsnode( Json::arrayValue );
        for ( auto& build : asd.builds )
        {
          auto abilityCmdIndex = resolveAbilityCmd( build.ability, build.command );

          Json::Value buildnode( Json::objectValue );
          buildnode["unit"] = g_unitMapping[build.unit];
          buildnode["unitName"] = build.unit;
          buildnode["abilityName"] = ( build.ability + "," + build.command );
          buildnode["ability"] = abilityCmdIndex;
          buildnode["time"] = build.time;

          dumpRequirementsJSON( build.requirements, requirements, nodes, buildnode );

          buildsnode.append( buildnode );
        }
        unitnode["builds"] = buildsnode;
      }

      if ( !asd.morphs.empty() )
      {
        Json::Value morphsnode( Json::arrayValue );
        for ( auto& morph : asd.morphs )
        {
          auto abilityCmdIndex = resolveAbilityCmd( morph.ability, morph.command );

          Json::Value morphnode( Json::objectValue );
          morphnode["unit"] = g_unitMapping[morph.unit];
          morphnode["unitName"] = morph.unit;
          morphnode["abilityName"] = ( morph.ability + "," + morph.command );
          morphnode["ability"] = abilityCmdIndex;
          morphnode["time"] = morph.time;

          dumpRequirementsJSON( morph.requirements, requirements, nodes, morphnode );

          morphsnode.append( morphnode );
        }
        unitnode["morphs"] = morphsnode;
      }

      if ( !asd.researches.empty() )
      {
        Json::Value resnode( Json::arrayValue );
        for ( auto& res : asd.researches )
        {
          if ( res.upgrade.size() < 2 )
            continue;

          auto abilityCmdIndex = resolveAbilityCmd( res.ability, res.command );

          Json::Value resentry( Json::objectValue );
          resentry["upgrade"] = g_upgradeMapping[res.upgrade];
          resentry["upgradeName"] = res.upgrade;
          resentry["abilityName"] = ( res.ability + "," + res.command );
          resentry["ability"] = abilityCmdIndex;
          resentry["time"] = res.time;
          resentry["minerals"] = res.minerals;
          resentry["vespene"] = res.vespene;

          dumpRequirementsJSON( res.requirements, requirements, nodes, resentry );

          resnode.append( resentry );
        }
        unitnode["researches"] = resnode;
      }

      auto numindex = std::to_string( g_unitMapping[asd.id] );

      racenode[numindex] = unitnode;
    }

    root[raceStr( entry.first )] = racenode;
  }

  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "  ";
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  writer->write( root, &out );

  out.close();
}

void readGameData( const string& path, UnitMap& units, AbilityMap& abilities, RequirementMap& requirements, RequirementNodeMap& nodes, FootprintMap& footprints )
{
  string unitDataPath = path + R"(\UnitData.xml)";

  size_t loops = 0;
  while ( true )
  {
    loops++;
    size_t notfound = 0;
    parseUnitData( unitDataPath, units, notfound );
    if ( notfound == 0 )
      break;
    if ( loops > 10 )
      throw exception( "Failed to parse unit data in 10 rounds, wtf?" );
  }

  string abilityDataPath = path + R"(\AbilData.xml)";
  parseAbilityData( abilityDataPath, abilities );

  string requirementDataPath = path + R"(\RequirementData.xml)";
  string requirementNodeDataPath = path + R"(\RequirementNodeData.xml)";
  parseRequirementData( requirementDataPath, requirementNodeDataPath, requirements, nodes );

  string footprintDataPath = path + R"(\FootprintData.xml)";
  parseFootprintData( footprintDataPath, footprints );
}

void readStableID( const string& path, NameToIDMapping& unitMapping, NameToIDMapping& abilityMapping, NameToIDMapping& upgradeMapping )
{
  Json::Value root;
  std::ifstream infile;
  infile.open( path, std::ifstream::in );
  if ( !infile.is_open() )
    throw std::exception( "could not open stableid.json" );
  infile >> root;
  infile.close();

  auto& units = root["Units"];
  for ( auto& unit : units )
  {
    unitMapping[unit["name"].asString()] = unit["id"].asUInt64();
  }

  auto& abilities = root["Abilities"];
  for ( auto& ability : abilities )
  {
    string name = ( ability["name"].asString() + "," );
    name.append( ability["index"].asString() );
    abilityMapping[name] = ability["id"].asUInt64();
  }

  auto& upgrades = root["Upgrades"];
  for ( auto& upgrade : upgrades )
  {
    upgradeMapping[upgrade["name"].asString()] = upgrade["id"].asUInt64();
  }
}

void dumpTechTreeText( const string& suffix, TechTree& tree )
{
  // dump techtree to txt for debug
  ofstream techDump;
  techDump.open( "techtree-" + suffix + ".txt" );
  for ( auto& root : tree )
  {
    techDump << root.id << std::endl;
    if ( !root.builds.empty() )
      techDump << "  builds" << std::endl;
    for ( auto& bld : root.builds )
      if ( !bld.unit.empty() )
        techDump << "    " << bld.unit << std::endl;
    if ( !root.morphs.empty() )
      techDump << "  morphs" << std::endl;
    for ( auto& mrph : root.morphs )
      if ( !mrph.unit.empty() )
        techDump << "    " << mrph.unit << std::endl;
    if ( !root.researches.empty() )
    {
      techDump << "  upgrades" << std::endl;
      for ( auto& res : root.researches )
        if ( !res.upgrade.empty() )
          techDump << "    " << res.upgrade << std::endl;
    }
    techDump << std::endl;
  }
  techDump.close();
}

void cleanupUnitCommandCards( UnitMap& units )
{
  for ( auto& unit : units )
  {
    std::set<std::string> usedCommands;
    for ( auto& card : unit.second.abilityCardsMap )
    {
      if ( card.second.removed || card.second.commands.empty() )
        continue;
      auto it = card.second.commands.begin();
      while ( it != card.second.commands.end() )
      {
        if ( usedCommands.find( ( *it ).second ) != usedCommands.end() )
          it = card.second.commands.erase( it );
        else
        {
          usedCommands.insert( ( *it ).second );
          it++;
        }
      }
    }
  }
}

int main()
{
  string rootPath;
  rootPath.reserve( MAX_PATH );

  GetCurrentDirectoryA( MAX_PATH, &rootPath[0] );

  string stableIDPath = rootPath.c_str(); // clone from c_str because internally rootPath is corrupted
  stableIDPath.append( R"(\stableid.json)" );

  readStableID( stableIDPath, g_unitMapping, g_abilityMapping, g_upgradeMapping );

  // sc2 uses incremental patches so these have to be in order
  vector<string> mods = {
    "core.sc2mod",
    "liberty.sc2mod",
    "swarm.sc2mod",
    "void.sc2mod",
    "voidmulti.sc2mod"
  };

  UnitMap units;
  AbilityMap abilities;
  RequirementMap requirements;
  RequirementNodeMap nodes;
  FootprintMap footprints;

  for ( auto mod : mods )
  {
    string modPath = rootPath.c_str(); // clone from c_str because internally rootPath is corrupted
    modPath.append( R"(\mods\)" + mod );

    printf_s( "[A] mod: %s\r\n", mod.c_str() );

    string gameDataPath = modPath + R"(\base.sc2data\GameData)";
    readGameData( gameDataPath, units, abilities, requirements, nodes, footprints );
  }

  cleanupUnitCommandCards( units );

  dumpUnits( units, footprints );

  dumpAbilities( abilities, requirements, nodes );

  TechTree zergTechTree;
  TechTree protossTechTree;
  TechTree terranTechTree;
  generateTechTree( units, abilities, Race_Zerg, zergTechTree );
  generateTechTree( units, abilities, Race_Protoss, protossTechTree );
  generateTechTree( units, abilities, Race_Terran, terranTechTree );

  TechMap techMap;
  techMap[Race_Zerg] = zergTechTree;
  techMap[Race_Protoss] = protossTechTree;
  techMap[Race_Terran] = terranTechTree;

  dumpTechTree( techMap, requirements, nodes );

  // dump footprints to text file with easy visualisation
  ofstream footDump;
  footDump.open( "footprints.txt" );
  for ( auto& fp : footprints )
  {
    if ( fp.second.removed )
      continue;

    char sdfsd[128];
    sprintf_s( sdfsd, 128, " (%i,%i,%i,%i)", fp.second.x, fp.second.y, fp.second.w, fp.second.h );
    footDump << fp.second.id << sdfsd << std::endl;
    for ( size_t y = 0; y < fp.second.h; y++ )
    {
      for ( size_t x = 0; x < fp.second.w; x++ )
      {
        auto idx = y * fp.second.w + x;
        footDump << ( fp.second.placement[idx] ? "x" : "." );
      }
      footDump << std::endl;
    }
    footDump << std::endl;
  }
  footDump.close();

  dumpTechTreeText( "zerg", techMap[Race_Zerg] );
  dumpTechTreeText( "protoss", techMap[Race_Protoss] );
  dumpTechTreeText( "terran", techMap[Race_Terran] );

  return EXIT_SUCCESS;
}