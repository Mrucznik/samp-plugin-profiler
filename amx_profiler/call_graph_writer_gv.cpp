// Copyright (c) 2011-2013, Zeex
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef AMX_PROFILER_CALL_GRAPH_WRITER_H
#define AMX_PROFILER_CALL_GRAPH_WRITER_H

#include <iostream>
#include <tuple>
#include <string>
#include "call_graph.h"
#include "call_graph_writer_gv.h"
#include "duration.h"
#include "function.h"
#include "function_statistics.h"

namespace amx_profiler {

void CallGraphWriterGV::Write(const CallGraph *graph) {
	*stream() << 
	"digraph \"Call graph of '" << script_name() << "'\" {\n"
	"	size=\"10,8\"; ratio=fill; rankdir=LR\n"
	"	node [style=filled];\n"
	;

	// Write basic graph (nodes + arrows).
	graph->Traverse([this](const CallGraphNode *node) {
		if (!node->callees().empty()) {
			std::string caller_name;
			if (node->stats()) {
				caller_name = node->stats()->function()->name();
			} else {
				caller_name = root_node_name();
			}
			for (auto c : node->callees()) {
				*stream() << "\t\"" << caller_name << "\" -> \"" << c->stats()->function()->name() 
					<< "\" [color=\"";
				// Arrow color is associated with callee type.
				std::string fn_type = c->stats()->function()->type();
				if (fn_type == "public") {
					*stream() << "#4B4E99";
				} else if (fn_type == "native") {
					*stream() << "#7C4B99";
				} else {
					*stream() << "#777777";
				}
				*stream() << "\"];\n";
			}
		}	
	});

	// Get maximum execution time.
	Duration max_time;
	graph->Traverse([&max_time, &graph](const CallGraphNode *node) {
		if (node != graph->sentinel()) {
			auto time = node->stats()->GetSelfTime();
			if (time > max_time) {
				max_time = time;
			}
		}
	});

	// Color nodes depending to draw attention to hot spots.
	graph->Traverse([&max_time, this, &graph](const CallGraphNode *node) {
		if (node != graph->sentinel()) {
			auto time = node->stats()->GetSelfTime();
			auto ratio = static_cast<double>(time.count()) / static_cast<double>(max_time.count());
			// We encode color in HSB.
			auto hsb = std::make_tuple(
				(1.0 - ratio) * 0.6, // hue
				(ratio * 0.9) + 0.1, // saturation
				1.0                  // brightness
			);
			*stream() << "\t\"" << node->stats()->function()->name() << "\" [color=\""
				<< std::get<0>(hsb) << ", "
				<< std::get<1>(hsb) << ", "
				<< std::get<2>(hsb) << "\""
			// Choose different shape depending on funciton type.
			<< ", shape=";
			std::string fn_type = node->stats()->function()->type();
			if (fn_type == "public") {
				*stream() << "octagon";
			} else if (fn_type == "native") {
				*stream() << "box";
			} else {
				*stream() << "oval";
			}
			*stream() << "];\n";
		} else {
			*stream() << "\t\"" << root_node_name() << "\" [shape=diamond];\n";
		}
	});

	*stream() <<
	"}\n";
}

} // namespace amx_profiler

#endif // !AMX_PROFILER_CALL_GRAPH_WRITER_H


