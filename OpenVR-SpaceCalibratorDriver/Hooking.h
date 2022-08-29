#pragma once

#include "Logging.h"

#ifdef __linux__
#include <unistd.h>
#include "string.h"
#include <sys/mman.h>
#else
#include <MinHook.h>
#endif


#include <map>
#include <string>

class IHook
{
public:
	const std::string name;

	IHook(const std::string &name) : name(name) { }
	virtual ~IHook() { }

	virtual void Destroy() = 0;

	static bool Exists(const std::string &name);
	static void Register(IHook *hook);
	static void Unregister(IHook *hook);
	static void DestroyAll();

private:
	static std::map<std::string, IHook *> hooks;
};

template<class FuncType> class Hook : public IHook
{
public:
	FuncType originalFunc = nullptr;
	Hook(const std::string &name) : IHook(name) { }

#ifdef __linux__
    template<typename T>
	bool CreateHookInObjectVTable(void *object, int vtableOffset, T* detourFunction)
	{
   		// For virtual objects, VC++ (and from what I can tell gcc)  adds a pointer to the vtable as the first member.
		// To access the vtable, we simply dereference the object.
		void **vtable = *((void ***)object);

		// The vtable itself is an array of pointers to member functions,
		// in the order they were declared in.
        originalFunc = (FuncType) vtable[vtableOffset];
        targetFunc = (void*) vtable[vtableOffset];

        uintptr_t otherPage = (uintptr_t) vtable & ~(uintptr_t) 4095;
        int err = mprotect((void*) otherPage, 8196, PROT_READ | PROT_WRITE);
        if(err){
            LOG("Failed to set memory protection %d-%s", err, strerror(errno));
        }
        else {
            //LOG("%s", "Setting vtable value");
            vtable[vtableOffset] = (void*) detourFunction;
            //LOG("%s", "Resetting permissions vtable value");
            mprotect((void*) otherPage, 8196, PROT_READ | PROT_EXEC);
        }


		LOG("Enabled Linux hook for %s", name.c_str());
		enabled = true;
		return true;
	}
#else
    template<typename T>
	bool CreateHookInObjectVTable(void *object, int vtableOffset, T* detourFunction)
	{
		// For virtual objects, VC++ (and from what I can tell gcc)  adds a pointer to the vtable as the first member.
		// To access the vtable, we simply dereference the object.
		void **vtable = *((void ***)object);

		// The vtable itself is an array of pointers to member functions,
		// in the order they were declared in.
		targetFunc = vtable[vtableOffset];

		auto err = MH_CreateHook(targetFunc, detourFunction, (LPVOID *)&originalFunc);
		if (err != MH_OK)
		{
			LOG("Failed to create hook for %s, error: %s", name.c_str(), MH_StatusToString(err));
			return false;
		}

		err = MH_EnableHook(targetFunc);
		if (err != MH_OK)
		{
			LOG("Failed to enable hook for %s, error: %s", name.c_str(), MH_StatusToString(err));
			MH_RemoveHook(targetFunc);
			return false;
		}

		LOG("Enabled hook for %s", name.c_str());
		enabled = true;
		return true;
	}
#endif

	void Destroy()
	{
#ifdef __linux__
        // should probably do something, but meh.
#else
		if (enabled)
		{
			MH_RemoveHook(targetFunc);
			enabled = false;
		}
#endif
	}

private:
	bool enabled = false;
	void* targetFunc = nullptr;
};
