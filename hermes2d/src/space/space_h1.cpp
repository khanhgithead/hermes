// This file is part of Hermes2D.
//
// Hermes2D is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// Hermes2D is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Hermes2D.  If not, see <http://www.gnu.org/licenses/>.

#include "space_h1.h"
namespace Hermes
{
  namespace Hermes2D
  {
    template<typename Scalar>
    H1Space<Scalar>::H1Space() : Space<Scalar>()
    {
    }

    template<typename Scalar>
    void H1Space<Scalar>::init(Shapeset* shapeset, int p_init, bool assign_dofs_init)
    {
      if (shapeset == nullptr)
      {
        this->shapeset = new H1Shapeset;
        this->own_shapeset = true;
      }

      this->precalculate_projection_matrix(2, this->proj_mat, this->chol_p);

      // enumerate basis functions
      if (assign_dofs_init)
      {
        // set uniform poly order in elements
        if (p_init < 1)
          throw Hermes::Exceptions::Exception("P_INIT must be >=  1 in an H1 space.");
        else
          this->set_uniform_order_internal(p_init, HERMES_ANY_INT);

        this->assign_dofs();
      }
    }

    template<typename Scalar>
    H1Space<Scalar>::H1Space(MeshSharedPtr mesh, EssentialBCs<Scalar>* essential_bcs, int p_init, Shapeset* shapeset)
      : Space<Scalar>(mesh, shapeset, essential_bcs)
    {
      init(shapeset, p_init);
    }

    template<typename Scalar>
    H1Space<Scalar>::H1Space(MeshSharedPtr mesh, int p_init, Shapeset* shapeset)
      : Space<Scalar>(mesh, shapeset, nullptr)
    {
      init(shapeset, p_init);
    }

    template<typename Scalar>
    H1Space<Scalar>::~H1Space()
    {
    }

    template<typename Scalar>
    void H1Space<Scalar>::copy(SpaceSharedPtr<Scalar> space, MeshSharedPtr new_mesh)
    {
      this->set_shapeset(space->get_shapeset(), true);
      this->precalculate_projection_matrix(2, this->proj_mat, this->chol_p);

      Space<Scalar>::copy(space, new_mesh);

      this->assign_dofs();
    }

    template<typename Scalar>
    void H1Space<Scalar>::set_shapeset(Shapeset *shapeset, bool clone)
    {
      if (!(shapeset->get_id() < 10))
        throw Hermes::Exceptions::Exception("Wrong shapeset type in H1Space<Scalar>::set_shapeset()");

      if (clone)
      {
        if (this->own_shapeset)
          delete this->shapeset;

        this->shapeset = shapeset->clone();
      }
      else
      {
        this->shapeset = shapeset;
        this->own_shapeset = false;
      }
    }

    template<typename Scalar>
    void H1Space<Scalar>::assign_vertex_dofs()
    {
      // Before assigning vertex DOFs, we must know which boundary vertex nodes are part of
      // a natural BC and which are part of an essential BC. The critical are those which
      // lie at an interface of both types of BCs and which must be treated as belonging
      // to the essential part. Unfortunately this has to be done on a per-space basis, as
      // the markers may have different meanings in different spaces. There is no way to
      // look at the adjacent edge nodes given a vertex node, thus we have to walk through
      // all elements in the mesh.

      // Vertex dofs.
      Element* e;
      this->vertex_functions_count = 0;
      for_all_active_elements(e, this->mesh)
      {
        int order = this->get_element_order(e->id);
        if (order > 0)
        {
          for (unsigned char i = 0; i < e->get_nvert(); i++)
          {
            Node* vn = e->vn[i];
            typename Space<Scalar>::NodeData* nd = this->ndata + vn->id;
            if (!vn->is_constrained_vertex() && nd->dof == this->H2D_UNASSIGNED_DOF)
            {
              if (nd->n == 0)
              {
                nd->dof = this->H2D_CONSTRAINED_DOF;
              }
              else
              {
                nd->dof = this->next_dof;
                this->next_dof++;
                this->vertex_functions_count++;
              }
              nd->n = 1;
            }
          }
        }
      }
    }

    template<typename Scalar>
    void H1Space<Scalar>::assign_edge_dofs()
    {
      // Edge dofs.
      Element* e;
      this->edge_functions_count = 0;
      for_all_active_elements(e, this->mesh)
      {
        int order = this->get_element_order(e->id);
        if (order > 0)
        {
          for (unsigned char i = 0; i < e->get_nvert(); i++)
          {
            Node* vn = e->vn[i];
            typename Space<Scalar>::NodeData* nd = this->ndata + vn->id;
            Node* en = e->en[i];
            nd = this->ndata + en->id;
            if (nd->dof == this->H2D_UNASSIGNED_DOF)
            {
              // If the edge node is not constrained, assign it dofs.
              if (en->ref > 1 || en->bnd || this->mesh->peek_vertex_node(en->p1, en->p2) != nullptr)
              {
                int ndofs = this->get_edge_order_internal(en) - 1;
                nd->n = ndofs;

                if (en->bnd)
                  if (this->essential_bcs != nullptr)
                    if (this->essential_bcs->get_boundary_condition(this->mesh->boundary_markers_conversion.get_user_marker(e->en[i]->marker).marker) != nullptr)
                      nd->dof = this->H2D_CONSTRAINED_DOF;
                    else
                    {
                      nd->dof = this->next_dof;
                      this->next_dof += ndofs;
                      this->edge_functions_count += ndofs;
                    }
                  else
                  {
                    nd->dof = this->next_dof;
                    this->next_dof += ndofs;
                    this->edge_functions_count += ndofs;
                  }
                else
                {
                  nd->dof = this->next_dof;
                  this->next_dof += ndofs;
                  this->edge_functions_count += ndofs;
                }
              }
              else // Constrained edge node.
                nd->n = -1;
            }
          }
        }
      }
    }

    template<typename Scalar>
    void H1Space<Scalar>::assign_bubble_dofs()
    {
      // Bubble dofs.
      Element* e;
      this->bubble_functions_count = 0;
      for_all_active_elements(e, this->mesh)
      {
        typename Space<Scalar>::ElementData* ed = &this->edata[e->id];
        ed->bdof = this->next_dof;
        ed->n = this->shapeset->get_num_bubbles(ed->order, e->get_mode());
        this->next_dof += ed->n;
        this->bubble_functions_count += ed->n;
      }
    }

    template<typename Scalar>
    void H1Space<Scalar>::get_vertex_assembly_list(Element* e, int iv, AsmList<Scalar>* al) const
    {
      Node* vn = e->vn[iv];
      typename Space<Scalar>::NodeData* nd = &this->ndata[vn->id];
      int index = this->shapeset->get_vertex_index(iv, e->get_mode());

      if (!vn->is_constrained_vertex()) // unconstrained
      {
        al->add_triplet(index, nd->dof, (nd->dof >= 0) ? 1.0 : *(nd->vertex_bc_coef));
      }
      else // constrained
      {
        for (int j = 0; j < nd->ncomponents; j++)
          if (nd->baselist[j].coef != (Scalar)0)
          {
          al->add_triplet(index, nd->baselist[j].dof, nd->baselist[j].coef);
          }
      }
    }

    template<typename Scalar>
    void H1Space<Scalar>::get_boundary_assembly_list_internal(Element* e, int surf_num, AsmList<Scalar>* al) const
    {
      Node* en = e->en[surf_num];
      typename Space<Scalar>::NodeData* nd = &this->ndata[en->id];

      if (nd->n >= 0) // unconstrained
      {
        if (nd->dof >= 0)
        {
          int ori = (e->vn[surf_num]->id < e->vn[e->next_vert(surf_num)]->id) ? 0 : 1;
          for (int j = 0, dof = nd->dof; j < nd->n; j++, dof++)
            al->add_triplet(this->shapeset->get_edge_index(surf_num, ori, j + 2, e->get_mode()), dof, 1.0);
        }
        else
        {
          for (int j = 0; j < nd->n; j++)
          {
            al->add_triplet(this->shapeset->get_edge_index(surf_num, 0, j + 2, e->get_mode()), -1, nd->edge_bc_proj[j + 2]);
          }
        }
      }
      else // constrained
      {
        int part = nd->part;
        int ori = part < 0 ? 1 : 0;
        if (part < 0) part ^= ~0;

        nd = &this->ndata[nd->base->id];
        for (int j = 0, dof = nd->dof; j < nd->n; j++, dof++)
          al->add_triplet(this->shapeset->get_constrained_edge_index(surf_num, j + 2, ori, part, e->get_mode()), dof, 1.0);
      }
    }

    template<typename Scalar>
    Scalar* H1Space<Scalar>::get_bc_projection(SurfPos* surf_pos, int order, EssentialBoundaryCondition<Scalar> *bc)
    {
      assert(order >= 1);
      Scalar* proj = malloc_with_check<H1Space<Scalar>, Scalar>(order + 1, this);

      if (bc->get_value_type() == BC_CONST)
      {
        proj[0] = proj[1] = bc->value_const;
      } // If the BC is not constant.
      else if (bc->get_value_type() == BC_FUNCTION)
      {
        surf_pos->t = surf_pos->lo;
        // Find out the (x, y) coordinates for the first endpoint.
        double x, y;
        Curve* curve = nullptr;
        CurvMap* cm = surf_pos->base->cm;
        if (cm)
          curve = cm->curves[surf_pos->surf_num];

        CurvMap::nurbs_edge(surf_pos->base, curve, surf_pos->surf_num, 2.0*surf_pos->t - 1.0, x, y);
        // Calculate.
        proj[0] = bc->value(x, y);
        surf_pos->t = surf_pos->hi;
        // Find out the (x, y) coordinates for the second endpoint.
        CurvMap::nurbs_edge(surf_pos->base, curve, surf_pos->surf_num, 2.0*surf_pos->t - 1.0, x, y);
        // Calculate.
        proj[1] = bc->value(x, y);
      }

      if (order-- > 1)
      {
        Quad1DStd quad1d;
        Scalar* rhs = proj + 2;
        int mo = quad1d.get_max_order();
        double2* pt = quad1d.get_points(mo);

        // get boundary values at integration points, construct rhs
        for (int i = 0; i < order; i++)
        {
          rhs[i] = 0.0;
          int ii = this->shapeset->get_edge_index(0, 0, i + 2, surf_pos->base->get_mode());
          for (int j = 0; j < quad1d.get_num_points(mo); j++)
          {
            double t = (pt[j][0] + 1) * 0.5, s = 1.0 - t;
            Scalar l = proj[0] * s + proj[1] * t;
            surf_pos->t = surf_pos->lo * s + surf_pos->hi * t;

            if (bc->get_value_type() == BC_CONST)
              rhs[i] += pt[j][1] * this->shapeset->get_fn_value(ii, pt[j][0], -1.0, 0, surf_pos->base->get_mode()) * (bc->value_const - l);
            // If the BC is not constant.
            else if (bc->get_value_type() == BC_FUNCTION)
            {
              // Find out the (x, y) coordinate.
              double x, y;
              Curve* curve = nullptr;
              CurvMap* cm = surf_pos->base->cm;
              if (cm)
                curve = cm->curves[surf_pos->surf_num];

              CurvMap::nurbs_edge(surf_pos->base, curve, surf_pos->surf_num, 2.0*surf_pos->t - 1.0, x, y);
              // Calculate.
              rhs[i] += pt[j][1] * this->shapeset->get_fn_value(ii, pt[j][0], -1.0, 0, surf_pos->base->get_mode()) * (bc->value(x, y) - l);
            }
          }
        }

        // solve the system using a precalculated Cholesky decomposed projection matrix
        cholsl(this->proj_mat, order, this->chol_p, rhs, rhs);
      }

      return proj;
    }

    template<typename Scalar>
    inline void H1Space<Scalar>::output_component(typename Space<Scalar>::BaseComponent*& current, typename Space<Scalar>::BaseComponent*& last, typename Space<Scalar>::BaseComponent* min,
      Node*& edge, typename Space<Scalar>::BaseComponent*& edge_dofs)
    {
      // if the dof is already in the list, just add half of the other coef
      if (last != nullptr && last->dof == min->dof)
      {
        last->coef += min->coef * 0.5;
        return;
      }

      // leave space for edge node dofs if they belong in front of the current minimum dof
      if (edge != nullptr && this->ndata[edge->id].dof <= min->dof)
      {
        edge_dofs = current;

        // (reserve space only if the edge dofs are not in the list yet)
        if (this->ndata[edge->id].dof != min->dof)
        {
          current += this->ndata[edge->id].n;
        }
        edge = nullptr;
      }

      // output new_ dof
      current->dof = min->dof;
      current->coef = min->coef * 0.5;
      last = current++;
    }

    template<typename Scalar>
    typename Space<Scalar>::BaseComponent* H1Space<Scalar>::merge_baselists(typename Space<Scalar>::BaseComponent* l1, int n1, typename Space<Scalar>::BaseComponent* l2, int n2,
      Node* edge, typename Space<Scalar>::BaseComponent*& edge_dofs, int& ncomponents)
    {
      // estimate the upper bound of the result size
      int max_result = n1 + n2;
      if (edge != nullptr)
        max_result += this->ndata[edge->id].n;

      typename Space<Scalar>::BaseComponent* result = malloc_with_check<typename Space<Scalar>::BaseComponent>(max_result);
      typename Space<Scalar>::BaseComponent* current = result;
      typename Space<Scalar>::BaseComponent* last = nullptr;

      // main loop - always output the component with smaller dof so that we get a sorted array
      int i1 = 0, i2 = 0;
      while (i1 < n1 && i2 < n2)
      {
        if (l1[i1].dof < l2[i2].dof)
          output_component(current, last, l1 + i1++, edge, edge_dofs);
        else
          output_component(current, last, l2 + i2++, edge, edge_dofs);
      }

      // finish the longer baselist
      while (i1 < n1)
        output_component(current, last, l1 + i1++, edge, edge_dofs);
      while (i2 < n2)
        output_component(current, last, l2 + i2++, edge, edge_dofs);

      // don't forget to reserve space for edge dofs if we haven't done that already
      if (edge != nullptr)
      {
        edge_dofs = current;
        current += this->ndata[edge->id].n;
      }

      ncomponents = current - result;
      if(ncomponents < max_result)
      {
typename Space<Scalar>::BaseComponent* reallocated_result = (typename Space<Scalar>::BaseComponent*) realloc(result, ncomponents * sizeof(typename Space<Scalar>::BaseComponent));
        if(edge_dofs != NULL)
        {
          edge_dofs = reallocated_result + (edge_dofs - result);
        }
        return reallocated_result;
      }
      else
        
      return result;
    }

    template<typename Scalar>
    void H1Space<Scalar>::update_constrained_nodes(Element* e, typename Space<Scalar>::EdgeInfo* ei0, typename Space<Scalar>::EdgeInfo* ei1, typename Space<Scalar>::EdgeInfo* ei2, typename Space<Scalar>::EdgeInfo* ei3)
    {
      unsigned char i, j;
      unsigned int k;
      typename Space<Scalar>::EdgeInfo* ei[4] = { ei0, ei1, ei2, ei3 };
      typename Space<Scalar>::NodeData* nd;

      if (this->get_element_order(e->id) == 0) return;

      // on non-refined elements all we have to do is update edge nodes lying on constrained edges
      if (e->active)
      {
        for (i = 0; i < e->get_nvert(); i++)
        {
          if (ei[i] != nullptr)
          {
            nd = &this->ndata[e->en[i]->id];
            nd->base = ei[i]->node;
            nd->part = ei[i]->part;
            if (ei[i]->ori)
              nd->part ^= ~0;
          }
        }
      }
      // the element has sons - update mid-edge constrained vertex nodes
      else
      {
        // create new_ edge infos where we don't have them yet
        typename Space<Scalar>::EdgeInfo ei_data[4];
        for (i = 0; i < e->get_nvert(); i++)
        {
          if (ei[i] == nullptr)
          {
            j = e->next_vert(i);
            Node* mid_vn = this->get_mid_edge_vertex_node(e, i, j);
            if (mid_vn != nullptr && mid_vn->is_constrained_vertex())
            {
              Node* mid_en = this->mesh->peek_edge_node(e->vn[i]->id, e->vn[j]->id);
              if (mid_en != nullptr)
              {
                ei[i] = ei_data + i;
                ei[i]->node = mid_en;
                ei[i]->part = -1;
                ei[i]->lo = -1.0;
                ei[i]->hi = 1.0;
                ei[i]->ori = (e->vn[i]->id < e->vn[j]->id) ? 0 : 1;
              }
            }
          }
        }

        // create a baselist for each mid-edge vertex node
        for (i = 0; i < e->get_nvert(); i++)
        {
          if (ei[i] == nullptr) continue;
          j = e->next_vert(i);

          Node* mid_vn = this->get_mid_edge_vertex_node(e, i, j);
          if (mid_vn == nullptr) continue;

          // endpoint vertex nodes
          Node* vn[2] = { e->vn[i], e->vn[j] };
          // constraining edge node
          Node* en = ei[i]->node;
          // base lists of v[0] and v[1]
          typename Space<Scalar>::BaseComponent *bl[2], dummy_bl[2];
          // number of components of bl[0] and bl[1]
          int nc[2] = { 0, 0 };

          // get baselists of vn[0] and vn[1] - pretend we have them even if they are unconstrained
          for (k = 0; k < 2; k++)
          {
            nd = &this->ndata[vn[k]->id];
            if (vn[k]->is_constrained_vertex())
            {
              bl[k] = nd->baselist;
              nc[k] = nd->ncomponents;
            }
            else // make up an artificial baselist
            {
              dummy_bl[k].dof = nd->dof;
              dummy_bl[k].coef = (nd->dof >= 0) ? 1.0 : (nd->vertex_bc_coef ? *nd->vertex_bc_coef : 0.);
              bl[k] = &dummy_bl[k];
              nc[k] = 1;
            }
          }

          // merge the baselists
          typename Space<Scalar>::BaseComponent* edge_dofs;
          nd = &this->ndata[mid_vn->id];
          nd->baselist = merge_baselists(bl[0], nc[0], bl[1], nc[1], en, edge_dofs, nd->ncomponents);
          this->bc_data_base_components.push_back(nd->baselist);

          // set edge node coeffs to function values of the edge functions
          double mid = (ei[i]->lo + ei[i]->hi) * 0.5;
          nd = &this->ndata[en->id];
          for (k = 0; k < nd->n; k++, edge_dofs++)
          {
            edge_dofs->dof = nd->dof + k;
            edge_dofs->coef = this->shapeset->get_fn_value(this->shapeset->get_edge_index(0, ei[i]->ori, k + 2, e->get_mode()), mid, -1.0, 0, e->get_mode());
          }
        }

        // create edge infos for half-edges
        typename Space<Scalar>::EdgeInfo  half_ei_data[4][2];
        typename Space<Scalar>::EdgeInfo* half_ei[4][2];
        for (i = 0; i < e->get_nvert(); i++)
        {
          if (!ei[i])
          {
            half_ei[i][0] = half_ei[i][1] = nullptr;
          }
          else
          {
            typename Space<Scalar>::EdgeInfo* h0 = half_ei[i][0] = half_ei_data[i];
            typename Space<Scalar>::EdgeInfo* h1 = half_ei[i][1] = half_ei_data[i] + 1;

            h0->node = h1->node = ei[i]->node;
            h0->part = (ei[i]->part + 1) * 2;
            h1->part = h0->part + 1;
            h0->hi = h1->lo = (ei[i]->lo + ei[i]->hi) / 2;
            h0->lo = ei[i]->lo;
            h1->hi = ei[i]->hi;
            h1->ori = h0->ori = ei[i]->ori;
          }
        }

        // recur to sons
        if (e->is_triangle())
        {
          update_constrained_nodes(e->sons[0], half_ei[0][0], nullptr, half_ei[2][1], nullptr);
          update_constrained_nodes(e->sons[1], half_ei[0][1], half_ei[1][0], nullptr, nullptr);
          update_constrained_nodes(e->sons[2], nullptr, half_ei[1][1], half_ei[2][0], nullptr);
          update_constrained_nodes(e->sons[3], nullptr, nullptr, nullptr, nullptr);
        }
        else if (e->sons[2] == nullptr) // 'horizontally' split quad
        {
          update_constrained_nodes(e->sons[0], ei[0], half_ei[1][0], nullptr, half_ei[3][1]);
          update_constrained_nodes(e->sons[1], nullptr, half_ei[1][1], ei[2], half_ei[3][0]);
        }
        else if (e->sons[0] == nullptr) // 'vertically' split quad
        {
          update_constrained_nodes(e->sons[2], half_ei[0][0], nullptr, half_ei[2][1], ei[3]);
          update_constrained_nodes(e->sons[3], half_ei[0][1], ei[1], half_ei[2][0], nullptr);
        }
        else // fully split quad
        {
          update_constrained_nodes(e->sons[0], half_ei[0][0], nullptr, nullptr, half_ei[3][1]);
          update_constrained_nodes(e->sons[1], half_ei[0][1], half_ei[1][0], nullptr, nullptr);
          update_constrained_nodes(e->sons[2], nullptr, half_ei[1][1], half_ei[2][0], nullptr);
          update_constrained_nodes(e->sons[3], nullptr, nullptr, half_ei[2][1], half_ei[3][0]);
        }
      }
    }

    template<typename Scalar>
    void H1Space<Scalar>::update_constraints()
    {
      Element* e;
      for_all_base_elements(e, this->mesh)
        update_constrained_nodes(e, nullptr, nullptr, nullptr, nullptr);
    }

    template HERMES_API class H1Space < double > ;
    template HERMES_API class H1Space < std::complex<double> > ;
  }
}