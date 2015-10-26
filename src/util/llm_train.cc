/**
 * @file    llm_train.cc
 * @author  Thomas M. Howard (tmhoward@csail.mit.edu)
 *          Matthew R. Walter (mwalter@csail.mit.edu)
 * @version 1.0
 *
 * @section LICENSE
 *
 * This file is part of h2sl.
 *
 * Copyright (C) 2014 by the Massachusetts Institute of Technology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html> or write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * @section DESCRIPTION
 *
 * A LLM model training program
 */

#include <iostream>

#include "h2sl/cv.h"
#include "h2sl/grounding_set.h"
#include "h2sl/region.h"
#include "h2sl/constraint.h"
#include "h2sl/llm.h"
#include "h2sl/dcg.h"
#include "llm_train_cmdline.h"

using namespace std;
using namespace h2sl;

void
evaluate_model( LLM* llm,
                vector< pair< unsigned int, LLM_X > >& examples ){
  vector< unsigned int > cvs;
  cvs.push_back( CV_FALSE );
  cvs.push_back( CV_TRUE );

  unsigned int num_correct = 0;
  for( unsigned int i = 0; i < examples.size(); i++ ){
    double pygx = llm->pygx( examples[ i ].first, examples[ i ].second, cvs );
    if( pygx < 0.75 ){
      cout << "example " << i << " had pygx " << pygx << endl;
      cout << "   filename:\"" << examples[ i ].second.filename() << "\"" << endl;
      cout << "         cv:" << examples[ i ].first << endl;
      if( dynamic_cast< const Region* >( examples[ i ].second.grounding() ) != NULL ){
        cout << "  grounding:" << *static_cast< const Region* >( examples[ i ].second.grounding() ) << endl; 
      } else if ( dynamic_cast< const Constraint* >( examples[ i ].second.grounding() ) != NULL ){
        cout << "  grounding:" << *static_cast< const Constraint* >( examples[ i ].second.grounding() ) << endl; 
      }
      for( unsigned int j = 0; j < examples[ i ].second.children().size(); j++ ){
        if( dynamic_cast< Region* >( examples[ i ].second.children()[ j ] ) != NULL ){
          cout << "children[" << j << "]:" << *static_cast< Region* >( examples[ i ].second.children()[ j ] ) << endl;
        } else if( dynamic_cast< Constraint* >( examples[ i ].second.children()[ j ] ) != NULL ){
          cout << "children[" << j << "]:" << *static_cast< Constraint* >( examples[ i ].second.children()[ j ] ) << endl;
        }
      }
      cout << "     phrase:" << *examples[ i ].second.phrase() << endl << endl;
    } else {
      num_correct++;
    }
  }

  cout << ( double )( num_correct ) / ( double )( examples.size() ) * 100.0 << " accuracy (" << num_correct << "/" << examples.size() << ")" << endl; 

  return;
}

unsigned int
evaluate_cv( const Grounding* grounding,
              const Grounding_Set* groundingSet ){
  unsigned int cv = CV_UNKNOWN;
  if( dynamic_cast< const Region* >( grounding ) != NULL ){
    const Region * region_grounding = dynamic_cast< const Region* >( grounding );
    cv = CV_FALSE;
    for( unsigned int i = 0; i < groundingSet->groundings().size(); i++ ){
      if( dynamic_cast< const Region* >( groundingSet->groundings()[ i ] ) ){
        if( *region_grounding == *dynamic_cast< const Region* >( groundingSet->groundings()[ i ] ) ){
          cv = CV_TRUE;
        }
      }
    }
  } else if ( dynamic_cast< const Constraint* >( grounding ) != NULL ){
    const Constraint* constraint_grounding = dynamic_cast< const Constraint* >( grounding );
    cv = CV_FALSE;
    for( unsigned int i = 0; i < groundingSet->groundings().size(); i++ ){
      if( dynamic_cast< const Constraint* >( groundingSet->groundings()[ i ] ) ){
        if( *constraint_grounding == *dynamic_cast< const Constraint* >( groundingSet->groundings()[ i ] ) ){
          cv = CV_TRUE;
        }
      }
    }
  }   
 
  return cv;
}

void
scrape_examples( const string& filename,
                  const Phrase* phrase,
                  const World* world,
                  const vector< pair< unsigned int, Grounding* > >& searchSpaces,
                  const vector< vector< unsigned int > >& correspondenceVariables,
                  vector< pair< unsigned int, LLM_X > >& examples ){
  const Grounding_Set * grounding_set = dynamic_cast< const Grounding_Set* >( phrase->grounding() );

  for( unsigned int i = 0; i < searchSpaces.size(); i++ ){
    examples.push_back( pair< unsigned int, LLM_X >( evaluate_cv( searchSpaces[ i ].second, grounding_set ), LLM_X( searchSpaces[ i ].second, phrase, world, correspondenceVariables[ searchSpaces[ i ].first ], filename ) ) );
    for( unsigned int j = 0; j < phrase->children().size(); j++ ){
      Grounding_Set * child_grounding_set = dynamic_cast< Grounding_Set* >( phrase->children()[ j ]->grounding() );
      if( child_grounding_set ){
        for( unsigned int k = 0; k < child_grounding_set->groundings().size(); k++ ){
          examples.back().second.children().push_back( child_grounding_set->groundings()[ k ] );
        }   
      }
    }
  }

  for( unsigned int i = 0; i < phrase->children().size(); i++ ){
    scrape_examples( filename, phrase->children()[ i ], world, searchSpaces, correspondenceVariables, examples );
  }
  return;
}

int
main( int argc,
      char* argv[] ) {
  gengetopt_args_info args;
  if( cmdline_parser( argc, argv, &args ) != 0 ){
    exit(1);
  }

  vector< DCG* > dcgs( args.inputs_num, NULL );
  vector< Phrase* > phrases( args.inputs_num, NULL );
  vector< World* > worlds( args.inputs_num, NULL );

  vector< pair< unsigned int, LLM_X > > examples;
  for( unsigned int i = 0; i < args.inputs_num; i++ ){
    cout << "reading file " << args.inputs[ i ] << endl;
    worlds[ i ] = new World();
    worlds[ i ]->from_xml( args.inputs[ i ] ); 
  
    phrases[ i ] = new Phrase();
    phrases[ i ]->from_xml( args.inputs[ i ] ); 

    dcgs[ i ] = new DCG();
    dcgs[ i ]->fill_search_spaces( worlds[ i ] );
    
    scrape_examples( args.inputs[ i ], phrases[ i ], worlds[ i ], dcgs[ i ]->search_spaces(), dcgs[ i ]->correspondence_variables(), examples );  
  }

  cout << "training with " << examples.size() << " examples" << endl;

  vector< Feature_Set* > feature_sets;
  for( unsigned int i = 0; i < args.threads_arg; i++ ){
    feature_sets.push_back( new Feature_Set() );
    feature_sets.back()->from_xml( args.feature_set_arg );
  }

  vector< LLM* > llms;
  for( unsigned int i = 0; i < args.threads_arg; i++ ){
    llms.push_back( new LLM( feature_sets[ i ] ) );
    llms.back()->weights().resize( llms.back()->feature_set()->size() );
  }

  LLM_Train* llm_train = new LLM_Train( llms );

  llm_train->train( examples, args.max_iterations_arg, args.lambda_arg, args.epsilon_arg );
 
  evaluate_model( llms.front(), examples );

  if( args.output_given ){
    llms.front()->to_xml( args.output_arg );
  }

  for( unsigned int i = 0; i < llms.size(); i++ ){
    if( llms[ i ] != NULL ){
      delete llms[ i ];
      llms[ i ] = NULL;
    }
  }
  llms.clear();
  
  for( unsigned int i = 0; i < feature_sets.size(); i++ ){
    if( feature_sets[ i ] != NULL ){
      delete feature_sets[ i ];
      feature_sets[ i ] = NULL;
    }
  } 
  feature_sets.clear();

  if( llm_train != NULL ){
    delete llm_train;
    llm_train = NULL;
  }

  return 0;
}
