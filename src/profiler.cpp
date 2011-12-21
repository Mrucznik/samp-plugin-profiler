// SA:MP Profiler plugin
//
// Copyright (c) 2011 Zeex
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <map>
#include <numeric>
#include <sstream>
#include <string>

#include "abstract_printer.h"
#include "function.h"
#include "profile.h"
#include "profiler.h"

#include "amx/amx.h"
#include "amx/amxdbg.h"

namespace {

int AMXAPI Debug(AMX *amx) {
	return samp_profiler::Profiler::Get(amx)->Debug();
}

// Reads from a code section at a given location.
inline cell ReadAmxCode(AMX *amx, cell where) {
	AMX_HEADER *hdr = reinterpret_cast<AMX_HEADER*>(amx->base);
	return *reinterpret_cast<cell*>(amx->base + hdr->cod + where);
}

} // anonymous namespace

namespace samp_profiler {

// statics
std::map<AMX*, Profiler*> Profiler::instances_;

Profiler::Profiler() {}

bool Profiler::IsScriptProfilable(AMX *amx) {
	uint16_t flags;
	amx_Flags(amx, &flags);

	if ((flags & AMX_FLAG_DEBUG) != 0) {
		return true;
	}

	if ((flags & AMX_FLAG_NOCHECKS) == 0) {
		return true;
	}

	return false;
}

// static
void Profiler::Attach(AMX *amx) {
	Profiler *prof = new Profiler(amx);
	instances_[amx] = prof;
	prof->Activate();
}

// static
void Profiler::Attach(AMX *amx, const DebugInfo &debug_info) {
	Attach(amx);
	Get(amx)->SetDebugInfo(debug_info);
}

// static
void Profiler::Detach(AMX *amx) {
	Profiler *prof = Profiler::Get(amx);
	if (prof != 0) {
		prof->Deactivate();
		delete prof;
	}
	instances_.erase(amx);
}

// static
Profiler *Profiler::Get(AMX *amx) {
	std::map<AMX*, Profiler*>::iterator it = instances_.find(amx);
	if (it != instances_.end()) {
		return it->second;
	}
	return 0;
}

Profiler::Profiler(AMX *amx) 
	: active_(false)
	, amx_(amx)
	, debug_(amx->debug)
{
	// Since PrintStats is done in AmxUnload and amx->base is already freed before
	// AmxUnload gets called, therefore both native and public tables are not accessible, 
	// from there, so they must be stored separately in some global place.
	AMX_HEADER *hdr = reinterpret_cast<AMX_HEADER*>(amx->base);
	AMX_FUNCSTUBNT *publics = reinterpret_cast<AMX_FUNCSTUBNT*>(amx->base + hdr->publics);
	AMX_FUNCSTUBNT *natives = reinterpret_cast<AMX_FUNCSTUBNT*>(amx->base + hdr->natives);
	int num_publics;
	amx_NumPublics(amx_, &num_publics);
	for (int i = 0; i < num_publics; i++) {
		public_names_.push_back(reinterpret_cast<char*>(amx->base + publics[i].nameofs));
	}
	int num_natives;
	amx_NumNatives(amx_, &num_natives);
	for (int i = 0; i < num_natives; i++) {
		native_names_.push_back(reinterpret_cast<char*>(amx->base + natives[i].nameofs));
	}
}

void Profiler::SetDebugInfo(const DebugInfo &info) {
	debug_info_ = info;
}

void Profiler::Activate() {
	if (!active_) {
		active_ = true;
		amx_SetDebugHook(amx_, ::Debug);
	}
}

bool Profiler::IsActive() const {
	return active_;
}

void Profiler::Deactivate() {
	if (active_) {
		active_ = false;
		amx_SetDebugHook(amx_, debug_);
	}
}

void Profiler::ResetStats() {
	functions_.clear();
}

void Profiler::PrintStats(std::ostream &stream, AbstractPrinter *printer) const {
	Profile profile;

	for (std::set<Function>::const_iterator iterator = functions_.begin(); 
			iterator != functions_.end(); ++iterator) 
	{
		std::string name;
		std::string type;

		switch (iterator->type()) {
		case Function::NATIVE:
			name = native_names_[iterator->index()];
			type = "native";
			break;
		case Function::PUBLIC:
			if (iterator->index() >= 0) {
				name = public_names_[iterator->index()];
				type = "public";
				break;
			} else if (iterator->index() == AMX_EXEC_MAIN) {
				name = "main";
				type = "main";
				break;
			}			
		case Function::NORMAL:			
			bool name_found = false;
			// Search in symbol table
			if (!name_found) {
				if (debug_info_.IsLoaded()) {
					name = debug_info_.GetFunction(iterator->address());
					if (!name.empty()) {	
						type = "normal";
						name_found = true;
					}
				}
			}
			// Not found
			if (!name_found) {
				std::stringstream ss;
				ss << "0x" << std::hex << iterator->address();
				ss >> name;
				type = "unknown";
			}			
		} 

		profile.push_back(ProfileEntry(name, type, iterator->time(), iterator->child_time(), 
				iterator->num_calls()));
	}

	printer->Print(stream, profile);
}

int Profiler::Debug() {
	// Get previous stack frame.
	cell prevFrame = amx_->stp;

	if (!call_stack_.IsEmpty()) {
		prevFrame = call_stack_.GetTop().frame();
	}

	// Check whether current frame is different.
	if (amx_->frm < prevFrame) {
		// Probably entered a function body (first BREAK after PROC).
		cell address = amx_->cip - 2*sizeof(cell);            
		// Check if we have a PROC opcode behind.
		if (ReadAmxCode(amx_, address) == 46) {
			EnterFunction(CallInfo(Function::Normal(address), amx_->frm));
		}
	} else if (amx_->frm > prevFrame) {
		if (call_stack_.GetTop().function().type() == Function::PUBLIC) { // entry points are handled by Exec
			// Left the function
			LeaveFunction(call_stack_.GetTop().function());
		}
	}

	if (debug_ != 0) {
		// Others could set their own debug hooks
		return debug_(amx_);
	}   

	return AMX_ERR_NONE;      
}

int Profiler::Callback(cell index, cell *result, cell *params) {
	EnterFunction(CallInfo(Function::Native(index), amx_->frm));
	int error = amx_Callback(amx_, index, result, params);
	LeaveFunction(Function::Native(index));
	return error;
}

int Profiler::Exec(cell *retval, int index) {	
	if (index >= 0 || index == AMX_EXEC_MAIN) {		
		AMX_HEADER *hdr = reinterpret_cast<AMX_HEADER*>(amx_->base);
		cell address = 0;
		if (index == AMX_EXEC_MAIN) {
			address = hdr->cip;
		} else {
			AMX_FUNCSTUBNT *publics = reinterpret_cast<AMX_FUNCSTUBNT*>(amx_->base + hdr->publics);
			address = publics[index].address;
		}        
		EnterFunction(CallInfo(Function::Public(index), amx_->stk - 3*sizeof(cell)));
		int error = amx_Exec(amx_, retval, index);
		LeaveFunction(Function::Public(index));
		return error;
	} else {
		return amx_Exec(amx_, retval, index);
	}
}

void Profiler::EnterFunction(const CallInfo &call) {
	call_stack_.Push(call);
	std::set<Function>::iterator iterator = functions_.find(call.function());
	if (iterator == functions_.end()) {
		Function f = call.function();
		f.IncreaseCalls();
		functions_.insert(f);
	} else {
		iterator->IncreaseCalls();
	}
}

void Profiler::LeaveFunction(const Function &function) {
	while (true) {
		CallInfo current = call_stack_.Pop();		
		std::set<Function>::iterator iterator = functions_.find(function);
		if (iterator != functions_.end()) {
			const Timer &timer = current.timer();
			// TODO: calculate actual child_time 
			iterator->AdjustTime(timer.total_time(), 0);
		}
		if (current.function() == function) {
			break;
		}
	}
}

} // namespace samp_profiler
