#include "graph_executor.hpp"
#include "graph_parser.hpp"
#include "elements/facade_foundation_algorithms.hpp"
#include <cstdio>
#include <string>
int main() {
  const std::string graph = R"({
    "version":"1.0",
    "nodes":[
      {"id":"box","type":"CreateBoxMesh","data":{"width":2.0,"height":2.0,"depth":2.0}},
      {"id":"lines","type":"ConvertLine","data":{"mode":"all"}},
      {"id":"out","type":"Output","data":{}}
    ],
    "edges":[
      {"source":"box","target":"lines","sourceHandle":"out","targetHandle":"in"},
      {"source":"lines","target":"out","sourceHandle":"out","targetHandle":"in"}
    ]
  })";
  char error[2048]={};
  pcg::internal::Graph g;
  auto pc = pcg::internal::parse_graph(graph.c_str(), g, error, sizeof(error));
  printf("parse=%d err=%s\n", (int)pc, error);
  // also direct algo on box only
  const std::string box_only = R"({"version":"1.0","nodes":[{"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},{"id":"out","type":"Output","data":{}}],"edges":[{"source":"box","target":"out","sourceHandle":"out","targetHandle":"in"}]})";
  pcg::internal::Graph g2; error[0]=0;
  pcg::internal::parse_graph(box_only.c_str(), g2, error, sizeof(error));
  pcg::internal::GraphExecutionResult r2;
  auto ec2 = pcg::internal::execute_graph(g2, 1, r2, error, sizeof(error));
  printf("box exec=%d geo=%p pts=%zu faces=%zu err=%s\n", (int)ec2, (void*)r2.source_geometry.get(),
    r2.source_geometry?r2.source_geometry->points().size():0,
    r2.source_geometry?r2.source_geometry->faces().size():0, error);
  if (r2.source_geometry) {
    auto s = pcg::internal::elements::convert_line_geometry(*r2.source_geometry, {"", "all"});
    printf("direct convert all=%zu\n", s.splines().size());
  }
  error[0]=0;
  pcg::internal::GraphExecutionResult r;
  auto ec = pcg::internal::execute_graph(g, 1, r, error, sizeof(error));
  printf("chain exec=%d err=%s\n", (int)ec, error);
  return 0;
}
