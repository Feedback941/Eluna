/*
* Copyright (C) 2010 - 2015 Eluna Lua Engine <http://emudevs.com/>
* This program is free software licensed under GPL version 3
* Please see the included DOCS/LICENSE.md for more information
*/

#ifndef _ELUNA_BINDING_H
#define _ELUNA_BINDING_H

#include "Common.h"
#include "LuaEngine.h"
#include "ElunaUtility.h"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
};

#ifdef WIN32
// VC++ complains about UniqueBind because one of its template types is really long.
#pragma warning(disable:4503)
#endif

class ElunaBind : public ElunaUtil::RWLockable
{
public:
    struct Binding
    {
        int functionReference;
        bool isTemporary;
        uint32 remainingShots;
        uint32 uniqueId;
        bool allowremoveall;

        
        uint8 _regtype;
        uint32 _id;
        uint64 _guid;
        uint32 _instanceId;
        uint32 _evt;
        int _funcref;

        ElunaBind* B;

        Binding(ElunaBind* _B, uint32 _uniqueId, int funcRef, uint32 shots, bool _allowremoveall) :
            functionReference(funcRef),
            isTemporary(shots != 0),
            remainingShots(shots),
            uniqueId(_uniqueId),
            allowremoveall(_allowremoveall),
            B(_B)
        {
        }

        ~Binding()
        {
            // Remove our function from the registry when the Binding is deleted.
            luaL_unref(B->E.L, LUA_REGISTRYINDEX, functionReference);

            // Remove from unique map so this binding can no longer be removed through it's ID
            B->E.regdata.erase(uniqueId);
        }
    };
    typedef std::vector<Binding> FunctionRefVector;
    typedef UNORDERED_MAP<int, FunctionRefVector> EventToFunctionsMap;

    Eluna& E;
    const char* groupName;

    ElunaBind(const char* bindGroupName, Eluna& _E) : E(_E), groupName(bindGroupName)
    {
    }

    virtual ~ElunaBind()
    {
        Clear();
    }

    // unregisters all registered functions and clears all registered events from the bindings
    virtual void Clear() { };

    // unregisters a function by it's uniqueid
    virtual void RemoveUnique(uint32 uniqueid) { };
};

template<typename T>
class EventBind : public ElunaBind
{
public:
    EventBind(const char* bindGroupName, Eluna& _E) : ElunaBind(bindGroupName, _E)
    {
    }

    void RemoveUnique(uint32 uniqueid) override
    {
        UNORDERED_MAP<uint32, Eluna::RegData>::const_iterator it = E.regdata.find(uniqueid);
        if (it == E.regdata.end())
            return;
        const Eluna::RegData& data = it->second;

        WriteGuard guard(GetLock());

        FunctionRefVector& funcrefvec = Bindings[data._evt];
        for (FunctionRefVector::const_iterator i = funcrefvec.begin(); i != funcrefvec.end(); ++i)
        {
            if ((*i).uniqueId == uniqueid)
            {
                funcrefvec.erase(i);
                return;
            }
        }

        if (Bindings[data._evt].empty())
            Bindings.erase(data._evt);
    }

    // unregisters all registered functions and clears all registered events from the bind std::maps (reset)
    void Clear() override
    {
        WriteGuard guard(GetLock());
        Bindings.clear();
    }

    void Clear(uint32 event_id)
    {
        WriteGuard guard(GetLock());
        Bindings.erase(event_id);
    }

    // Pushes the function references and updates the counters on the binds and erases them if the counter would reach 0
    void PushFuncRefs(lua_State* L, int event_id)
    {
        WriteGuard guard(GetLock());

        for (FunctionRefVector::iterator it = Bindings[event_id].begin(); it != Bindings[event_id].end();)
        {
            FunctionRefVector::iterator it_old = it++;
            Binding& binding = (*it_old);

            lua_rawgeti(L, LUA_REGISTRYINDEX, binding.functionReference);

            if (binding.isTemporary)
            {
                binding.remainingShots--;
                if (binding.remainingShots == 0)
                    Bindings[event_id].erase(it_old);
            }
        }

        if (Bindings[event_id].empty())
            Bindings.erase(event_id);
    };

    void Insert(uint32 uniqueid, int eventId, int funcRef, uint32 shots, bool allowremoveall) // Inserts a new registered event
    {
        WriteGuard guard(GetLock());
        Bindings[eventId].push_back(Binding(this, uniqueid, funcRef, shots, allowremoveall));
    }

    // Checks if there are events for ID
    bool HasEvents(T eventId)
    {
        ReadGuard guard(GetLock());

        if (!E.IsEnabled())
            return false;

        if (Bindings.empty())
            return false;
        if (Bindings.find(eventId) == Bindings.end())
            return false;
        return true;
    }

    EventToFunctionsMap Bindings; // Binding store Bindings[eventId] = {(funcRef, counter)};
};

template<typename T>
class EntryBind : public ElunaBind
{
public:
    typedef UNORDERED_MAP<uint32, EventToFunctionsMap> EntryToEventsMap;

    EntryBind(const char* bindGroupName, Eluna& _E) : ElunaBind(bindGroupName, _E)
    {
    }

    void RemoveUnique(uint32 uniqueid) override
    {
        UNORDERED_MAP<uint32, Eluna::RegData>::const_iterator it = E.regdata.find(uniqueid);
        if (it == E.regdata.end())
            return;
        const Eluna::RegData& data = it->second;

        WriteGuard guard(GetLock());
        FunctionRefVector& funcrefvec = Bindings[data._id][data._evt];
        for (FunctionRefVector::const_iterator i = funcrefvec.begin(); i != funcrefvec.end(); ++i)
        {
            if ((*i).uniqueId == uniqueid)
            {
                funcrefvec.erase(i);
                return;
            }
        }

        if (Bindings[data._id][data._evt].empty())
            Bindings[data._id].erase(data._evt);

        if (Bindings[data._id].empty())
            Bindings.erase(data._id);
    }

    // unregisters all registered functions and clears all registered events from the bindmap
    void Clear() override
    {
        WriteGuard guard(GetLock());
        Bindings.clear();
    }

    void Clear(uint32 entry, uint32 event_id)
    {
        WriteGuard guard(GetLock());
        Bindings[entry].erase(event_id);
    }

    // Pushes the function references and updates the counters on the binds and erases them if the counter would reach 0
    void PushFuncRefs(lua_State* L, int event_id, uint32 entry)
    {
        WriteGuard guard(GetLock());

        for (FunctionRefVector::iterator it = Bindings[entry][event_id].begin(); it != Bindings[entry][event_id].end();)
        {
            FunctionRefVector::iterator it_old = it++;
            Binding& binding = (*it_old);

            lua_rawgeti(L, LUA_REGISTRYINDEX, binding.functionReference);

            if (binding.isTemporary)
            {
                binding.remainingShots--;
                if (binding.remainingShots == 0)
                    Bindings[entry][event_id].erase(it_old);
            }
        }

        if (Bindings[entry][event_id].empty())
            Bindings[entry].erase(event_id);

        if (Bindings[entry].empty())
            Bindings.erase(entry);
    };

    void Insert(uint32 uniqueid, uint32 entryId, int eventId, int funcRef, uint32 shots, bool allowremoveall) // Inserts a new registered event
    {
        WriteGuard guard(GetLock());
        Bindings[entryId][eventId].push_back(Binding(this, uniqueid, funcRef, shots, allowremoveall));
    }

    // Returns true if the entry has registered binds
    bool HasEvents(T eventId, uint32 entryId)
    {
        ReadGuard guard(GetLock());

        if (Bindings.empty())
            return false;

        EntryToEventsMap::const_iterator itr = Bindings.find(entryId);
        if (itr == Bindings.end())
            return false;

        return itr->second.find(eventId) != itr->second.end();
    }

    bool HasEvents(uint32 entryId)
    {
        ReadGuard guard(GetLock());

        if (!E.IsEnabled())
            return false;

        if (Bindings.empty())
            return false;

        return Bindings.find(entryId) != Bindings.end();
    }

    EntryToEventsMap Bindings; // Binding store Bindings[entryId][eventId] = {(funcRef, counter)};
};

template<typename T>
class UniqueBind : public ElunaBind
{
public:
    typedef UNORDERED_MAP<uint32, EventToFunctionsMap> InstanceToEventsMap;
    typedef UNORDERED_MAP<uint64, InstanceToEventsMap> GUIDToInstancesMap;

    UniqueBind(const char* bindGroupName, Eluna& _E) : ElunaBind(bindGroupName, _E)
    {
    }

    void RemoveUnique(uint32 uniqueid) override
    {
        UNORDERED_MAP<uint32, Eluna::RegData>::const_iterator it = E.regdata.find(uniqueid);
        if (it == E.regdata.end())
            return;
        const Eluna::RegData& data = it->second;

        WriteGuard guard(GetLock());
        FunctionRefVector& funcrefvec = Bindings[data._guid][data._instanceId][data._evt];
        for (FunctionRefVector::const_iterator i = funcrefvec.begin(); i != funcrefvec.end(); ++i)
        {
            if ((*i).uniqueId == uniqueid)
            {
                funcrefvec.erase(i);
                return;
            }
        }

        if (Bindings[data._guid][data._instanceId][data._evt].empty())
            Bindings[data._guid][data._instanceId].erase(data._evt);

        if (Bindings[data._guid][data._instanceId].empty())
            Bindings[data._guid].erase(data._instanceId);

        if (Bindings[data._guid].empty())
            Bindings.erase(data._guid);
    }

    // unregisters all registered functions and clears all registered events from the bindmap
    void Clear() override
    {
        WriteGuard guard(GetLock());
        Bindings.clear();
    }

    void Clear(uint64 guid, uint32 instanceId, uint32 event_id)
    {
        WriteGuard guard(GetLock());
        Bindings[guid][instanceId].erase(event_id);
    }

    // Pushes the function references and updates the counters on the binds and erases them if the counter would reach 0
    void PushFuncRefs(lua_State* L, int event_id, uint64 guid, uint32 instanceId)
    {
        WriteGuard guard(GetLock());
        FunctionRefVector& v = Bindings[guid][instanceId][event_id];

        for (FunctionRefVector::iterator it = v.begin(); it != v.end();)
        {
            FunctionRefVector::iterator it_old = it++;
            Binding& binding = (*it_old);

            lua_rawgeti(L, LUA_REGISTRYINDEX, binding.functionReference);

            if (binding.isTemporary)
            {
                binding.remainingShots--;
                if (binding.remainingShots == 0)
                    v.erase(it_old);
            }
        }

        if (Bindings[guid][instanceId][event_id].empty())
            Bindings[guid][instanceId].erase(event_id);

        if (Bindings[guid][instanceId].empty())
            Bindings[guid].erase(instanceId);

        if (Bindings[guid].empty())
            Bindings.erase(guid);
    };

    void Insert(uint32 uniqueid, uint64 guid, uint32 instanceId, int eventId, int funcRef, uint32 shots, bool allowremoveall) // Inserts a new registered event
    {
        WriteGuard guard(GetLock());
        Bindings[guid][instanceId][eventId].push_back(Binding(this, uniqueid, funcRef, shots, allowremoveall));
    }

    // Returns true if the entry has registered binds
    bool HasEvents(T eventId, uint64 guid, uint32 instanceId)
    {
        ReadGuard guard(GetLock());

        if (Bindings.empty())
            return false;

        GUIDToInstancesMap::const_iterator itr = Bindings.find(guid);
        if (itr == Bindings.end())
            return false;

        InstanceToEventsMap::const_iterator it = itr->second.find(instanceId);
        if (it == itr->second.end())
            return false;

        return it->second.find(eventId) != it->second.end();
    }

    bool HasEvents(uint64 guid, uint32 instanceId)
    {
        ReadGuard guard(GetLock());

        if (Bindings.empty())
            return false;

        GUIDToInstancesMap::const_iterator itr = Bindings.find(guid);
        if (itr == Bindings.end())
            return false;

        return itr->second.find(instanceId) != itr->second.end();
    }

    GUIDToInstancesMap Bindings; // Binding store Bindings[guid][instanceId][eventId] = {(funcRef, counter)};
};

#endif
