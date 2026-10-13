// SPDX-License-Identifier: GPL-3.0-or-later
#include "../native/processes.hpp"
#include <cassert>
#include <string>
using namespace launcher;
int main(){
    ProcessSnapshot state;
    const std::string sample=R"({"processes":[{"pid":321,"name":"ftpserver.elf","memory":8.5,"is_daemon":true},{"pid":22,"name":"SceShellCore","memory":100.0,"is_daemon":false},{"pid":9021,"name":"elfldr.elf","memory":3.25,"is_daemon":true}]})";
    assert(parse_process_snapshot(sample,state));
    assert(state.available&&state.processes.size()==3);
    assert(state.processes[0].pid==321&&state.processes[0].name=="ftpserver.elf");
    assert(state.processes[0].memoryMiB==8.5);
    assert(process_is_stoppable(state.processes[0]));
    assert(!process_is_stoppable(state.processes[1]));
    assert(!process_is_stoppable(state.processes[2]));
    assert(!process_is_stoppable({56,"pldmgr.elf",5.0,true}));
    assert(!process_is_stoppable({56,"mini-syscore.elf",5.0,true}));
    assert(!process_is_stoppable({56,"usbscan.elf",5.0,true}));
    assert(!parse_process_snapshot("not JSON",state));
    assert(!parse_process_snapshot(R"({"processes":[{"pid":-5,"name":"foo.elf","memory":2,"is_daemon":true})",state));
    assert(parse_process_snapshot(R"({"processes":[{"pid":33,"name":"file\"name.elf","memory":0,"is_daemon":true}]})",state));
    assert(state.processes[0].name=="file\"name.elf");
    return 0;
}
