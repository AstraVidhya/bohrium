/*
This file is part of Bohrium and copyright (c) 2012 the Bohrium
team <http://www.bh107.org>.

Bohrium is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 3
of the License, or (at your option) any later version.

Bohrium is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the
GNU Lesser General Public License along with Bohrium.

If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>

#include <bh_component.h>
#include <bh_dag.h>

using namespace std;
using namespace boost;
using namespace bohrium::dag;

static int filter_count=1;
void filter(const bh_ir &bhir)
{
    GraphDW dag;

    from_kernels(bhir.kernel_list, dag);
    vector<GraphDW> dags;
    split(dag, dags);
    int i=0;
    BOOST_FOREACH(GraphDW &d, dags)
    {
        char filename[8000];
        snprintf(filename, 8000, "dag-%d-%d.dot", filter_count, ++i);
        pprint(d, filename);
    }
    char filename[8000];
    snprintf(filename, 8000, "dag-%d.dot", filter_count++);
    printf("fuseprinter: writing dag('%s').\n", filename);
    pprint(dag, filename);
}
