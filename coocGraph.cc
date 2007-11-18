#include "coocGraph.h"
#include "common.h"

#include<boost/tuple/tuple.hpp> // for "tie"

// Tokenizer
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>

using namespace boost;
using namespace std;

//______________________________________________________________________________
// Common functions


CoocGraph::vertex_descriptor CoocGraph::findOrInsertNode(const string & str) {

  CoocGraph::vertex_descriptor u;
  std::map<string, CoocGraph::vertex_descriptor>::iterator it;
  bool insertedP;

  tie(it, insertedP) = _nodeMap.insert(make_pair(str, CoocGraph::vertex_descriptor()));
  if (insertedP) {
    // new vertex
    u =  add_vertex(g);
    put(vertex_name, g, u, str);
    put(vertex_cfreq, g, u, 0);
    it->second = u;
  } else {
    u = it->second;
  }
  return u;
}

CoocGraph::edge_descriptor CoocGraph::findOrInsertEdge(CoocGraph::vertex_descriptor u, 
						       CoocGraph::vertex_descriptor v ) {
  
  CoocGraph::edge_descriptor e;
  bool exists;

  tie (e,exists) = edge(u, v, g);
  if(!exists) {
    e = add_edge(u, v, g).first;
    // Initialize new edge
    put(edge_freq, g, e, 0.0);
  }
  return e;
}

std::pair<CoocGraph::vertex_descriptor, bool> 
CoocGraph::getVertexByName(const std::string & str) const {


  map<string, CoocGraph::vertex_descriptor>::const_iterator it = _nodeMap.find(str);

  if (it == _nodeMap.end()) {
    return std::make_pair(CoocGraph::vertex_descriptor(), false);
  }
  return std::make_pair(it->second, true);
}

//______________________________________________________________________________
// Copy and remove isolated vertices

CoocGraph::CoocGraph(CoocGraph & cooc) {

  map<CoocGraph::vertex_descriptor, CoocGraph::vertex_descriptor> nMap;

  CoocGraph::vertex_iterator v_it, v_end;
  for(tie(v_it, v_end) = vertices(cooc.g); v_it != v_end; ++v_it) {
    if (out_degree(*v_it, cooc.g) > 0) {
      CoocGraph::vertex_descriptor u = findOrInsertNode(get(vertex_name, cooc.g, *v_it));
      put(vertex_cfreq, g, u,
	  get(vertex_cfreq, cooc.g, *v_it));
      nMap[*v_it] = u;
    }
  }
  CoocGraph::edge_iterator e_it, e_end;
  for(tie(e_it, e_end) = edges(cooc.g); e_it != e_end; ++e_it) {
    CoocGraph::edge_descriptor e = findOrInsertEdge(nMap[source(*e_it, cooc.g)],
						    nMap[target(*e_it, cooc.g)]);
    put(edge_freq, g, e,
	get(edge_freq, cooc.g, *e_it));
  }
  _docN = cooc._docN;
}

void CoocGraph::remove_isolated_vertices() {

  CoocGraph coog;
  map<CoocGraph::vertex_descriptor, CoocGraph::vertex_descriptor> nMap;

  CoocGraph::vertex_iterator v_it, v_end;
  for(tie(v_it, v_end) = vertices(g); v_it != v_end; ++v_it) {
    if (out_degree(*v_it, g) > 0) {
      CoocGraph::vertex_descriptor u = coog.findOrInsertNode(get(vertex_name, g, *v_it));
      put(vertex_cfreq, coog.g, u,
	  get(vertex_cfreq, g, *v_it));
      nMap[*v_it] = u;
    }
  }
  CoocGraph::edge_iterator e_it, e_end;
  for(tie(e_it, e_end) = edges(g); e_it != e_end; ++e_it) {
    CoocGraph::edge_descriptor e = coog.findOrInsertEdge(nMap[source(*e_it, g)],
							 nMap[target(*e_it, g)]);
    put(edge_freq, coog.g, e,
	get(edge_freq, g, *e_it));
  }

  // swap cooGraphs

  g.swap(coog.g);
  _nodeMap.swap(coog._nodeMap);

}

//______________________________________________________________________________
// Functions for reading files

void CoocGraph::insert_doc(vector<CoocGraph::vertex_descriptor> & doc) {
  vector<CoocGraph::vertex_descriptor>::iterator w = doc.begin(); 
  vector<CoocGraph::vertex_descriptor>::iterator end = doc.end();
  for(; w != end; ++w) {

    put(vertex_cfreq, g, *w,
	get(vertex_cfreq, g, *w) + 1);

    vector<CoocGraph::vertex_descriptor>::iterator it = w;
    ++it;    
    for(; it != end; ++it) {
      CoocGraph::edge_descriptor e = findOrInsertEdge(*w, *it);
      put(edge_freq, g, e,
	  get(edge_freq, g, e) + 1);      
    }
  }
}


string next_notempty(ifstream & fh) {
  string l;
  while(fh) {
    getline(fh, l, '\n');
    string::const_iterator sIt = l.begin();
    string::const_iterator sItEnd = l.end();
    while(sIt != sItEnd && isspace(*sIt)) ++sIt;
    if (sIt != sItEnd) return l;
  }
  return string();
}

void CoocGraph::fill_cograph(ifstream & fh) {

  string line;
  char_separator<char> sep(" ");

  while(fh) {

    vector<string> fields;

    line = next_notempty(fh); // docId
    if (!fh) break;
    line = next_notempty(fh);
    tokenizer<char_separator<char> > tok(line, sep);
    copy(tok.begin(), tok.end(), back_inserter(fields));
    if (0 == fields.size()) continue;
    _docN++;
    // erase duplicates
    set<string> docWords;
    for(vector<string>::iterator it_ = fields.begin(); it_ != fields.end(); ++it_)
      docWords.insert(*it_);
    
    vector<CoocGraph::vertex_descriptor> V;
    for(set<string>::const_iterator sit = docWords.begin(); sit != docWords.end(); ++sit) {
      V.push_back(findOrInsertNode(*sit));
    }
    insert_doc(V); // Insert cooc information to graph
  }
}

//______________________________________________________________________________
// Chi square and prunning


struct EdgeZeroChsq {

  const CoocGraph::boost_graph_t & g;
  float f;
  EdgeZeroChsq(const CoocGraph::boost_graph_t & g_, float f_) : g(g_), f(f_) {}
  bool operator() (const CoocGraph::edge_descriptor & e) {
    return (get(edge_freq, g, e) == f);
  }
};


void CoocGraph::chisq_prune(float threshold) {

   CoocGraph::edge_iterator e, end;
   tie(e, end) = edges(g);
   for(; e != end; ++e) {

     CoocGraph::vertex_descriptor vi = source(*e, g);
     CoocGraph::vertex_descriptor vj = target(*e, g);

     // Confusion matrix O
     size_t o11 = static_cast<size_t>(get(edge_freq, g, *e));
     size_t o12 = get(vertex_cfreq, g, vi) - o11;
     size_t o21 = get(vertex_cfreq, g, vj) - o11;
     size_t o22 = _docN - (get(vertex_cfreq, g, vi) + get(vertex_cfreq, g, vj) - o11);

     size_t a = o11 * o22 - o12 * o21;
     size_t b = (o11 + o12) * (o11 + o21) * (o12 + o22) + (o21 + o22);
  
     double chsq = static_cast<double>(_docN*a*a) / static_cast<double>(b);
     if (chsq < threshold) put(edge_freq, g , *e, 0.0);
     else put(edge_freq, g , *e, static_cast<float>(chsq));     
   }
   remove_edge_if(EdgeZeroChsq(g, 0.0), g);
}

//______________________________________________________________________________
// Display

void write_vertex(ostream & o, const CoocGraph::vertex_descriptor & v,
		  const CoocGraph::boost_graph_t & g) {

  o << get(vertex_name, g, v) << ":" << get(vertex_cfreq, g, v) << ":";
}

//______________________________________________________________________________
// Streaming
// Note: uses template functions in common.h

const size_t magic_id = 0x071109;


// write

ofstream & write_vertex_to_stream(ofstream & o,
				  const CoocGraph::boost_graph_t & g,
				  const CoocGraph::vertex_descriptor & v) {
  string name;

  write_atom_to_stream(o, get(vertex_name, g, v));
  write_atom_to_stream(o, get(vertex_cfreq, g, v));
  return o;
}

ofstream & write_edge_to_stream(ofstream & o,
				const CoocGraph::boost_graph_t & g,
				const CoocGraph::edge_descriptor & e) {

  size_t uIdx = get(vertex_index, g, source(e,g));
  size_t vIdx = get(vertex_index, g, target(e,g));
  float freq = get(edge_freq, g, e);

  o.write(reinterpret_cast<const char *>(&vIdx), sizeof(vIdx));
  o.write(reinterpret_cast<const char *>(&uIdx), sizeof(uIdx));
  o.write(reinterpret_cast<const char *>(&freq), sizeof(freq));

  return o;
}


std::ofstream & CoocGraph::write_to_stream(std::ofstream & o) const {

  write_atom_to_stream(o, magic_id);

  write_atom_to_stream(o, _docN);
  write_set_to_stream(o, _docSet);
  write_map_to_stream(o, _nodeMap);

  write_atom_to_stream(o, magic_id);  

  // The graph
  size_t vertex_n = boost::num_vertices(g);

  write_atom_to_stream(o, vertex_n);
  CoocGraph::vertex_iterator v_it, v_end;
  tie(v_it, v_end) = vertices(g);
  for(; v_it != v_end; ++v_it) {
    write_vertex_to_stream(o, g, *v_it);
  }

  write_atom_to_stream(o, magic_id);

  size_t edge_n = boost::num_edges(g);

  write_atom_to_stream(o, edge_n);
  CoocGraph::edge_iterator e_it, e_end;

  tie(e_it, e_end) = edges(g);
  for(; e_it != e_end; ++e_it) {
    write_edge_to_stream(o, g, *e_it);
  }

  write_atom_to_stream(o, magic_id);

  return o;
};

// Read

CoocGraph::vertex_descriptor 
read_vertex_from_stream(ifstream & is, 
			CoocGraph::boost_graph_t & g) {

  string name;
  size_t cfreq;

  read_atom_from_stream(is, name);
  read_atom_from_stream(is, cfreq);
  CoocGraph::vertex_descriptor v = add_vertex(g);
  put(vertex_name, g, v, name);
  //  put(vertex_wname, g, v, wname);
  put(vertex_cfreq, g, v, cfreq);
  return v;
}

CoocGraph::edge_descriptor 
read_edge_from_stream(ifstream & is, 
		      CoocGraph::boost_graph_t & g) {

  size_t uIdx;
  size_t vIdx; 
  float freq;
  bool insertedP;
  CoocGraph::edge_descriptor e;

  read_atom_from_stream(is, uIdx);
  read_atom_from_stream(is, vIdx);
  read_atom_from_stream(is, freq);
  tie(e, insertedP) = add_edge(uIdx, vIdx, g);
  assert(insertedP);
  put(edge_freq, g, e, freq);

  return e;
}


void CoocGraph::read_from_stream (std::ifstream & is) {
  size_t vertex_n;
  size_t edge_n;
  size_t i;
  size_t id;

  read_atom_from_stream(is, id);
  if(id != magic_id) {
    cerr << "Error: invalid id (filename is a coocGraph?)" << endl;
  }

  read_atom_from_stream(is, _docN);
  read_set_from_stream(is, _docSet);
  read_map_from_stream(is, _nodeMap);

  read_atom_from_stream(is, id);
  if(id != magic_id) {
    cerr << "Error: invalid id after reading maps" << endl;
  }

  read_atom_from_stream(is, vertex_n);
  for(i=0; i<vertex_n; ++i) {
    read_vertex_from_stream(is, g);
  }

  read_atom_from_stream(is, id);
  if(id != magic_id) {
    cerr << "Error: invalid id after reading vertices" << endl;
  }

  read_atom_from_stream(is, edge_n);
  for(i=0; i<edge_n; ++i) {
    read_edge_from_stream(is, g);
  }

  read_atom_from_stream(is, id);
  if(id != magic_id) {
    cerr << "Error: invalid id after reading edges" << endl;
  }

//   graph_traits<DisambG>::vertex_iterator v_it, v_end;
//   tie(v_it, v_end) = vertices(g);
//   for(; v_it != v_end; ++v_it) {
//     w2syns[get(vertex_name, g, *v_it)] = *v_it;
//   }
}



void CoocGraph::write_to_binfile (const string & fName) const {

  ofstream fo(fName.c_str(),  ofstream::binary|ofstream::out);
  if (!fo) {
    cerr << "Error: can't create" << fName << endl;
    exit(-1);
  }
  write_to_stream(fo);
}


void CoocGraph::read_from_binfile (const std::string & fName) {
  ifstream fh(fName.c_str(),  ifstream::binary);
  if (!fh) {
    cerr << "Error: can't read " << fName << endl;
    exit(-1);
  }
  read_from_stream(fh);
}


/*
 * Local Variables:
 * mode: c++
 * compile-command: "make -f makefile_cooc"
 * End:
 */
