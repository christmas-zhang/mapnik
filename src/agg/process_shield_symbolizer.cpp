/*****************************************************************************
 *
 * This file is part of Mapnik (c++ mapping toolkit)
 *
 * Copyright (C) 2011 Artem Pavlenko
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *****************************************************************************/

// mapnik
#include <mapnik/agg_renderer.hpp>
#include <mapnik/graphics.hpp>
#include <mapnik/text/symbolizer_helpers.hpp>
#include <mapnik/text/renderer.hpp>

namespace mapnik {

template <typename T>
void  agg_renderer<T>::process(shield_symbolizer const& sym,
                               mapnik::feature_impl & feature,
                               proj_transform const& prj_trans)
{
    text_symbolizer_helper helper(
            sym, feature, prj_trans,
            width_, height_,
            scale_factor_,
            t_, font_manager_, *detector_,
            clipping_extent());

    agg_text_renderer<T> ren(*current_buffer_, sym.get_halo_rasterizer(), sym.comp_op(), scale_factor_, font_manager_.get_stroker());

    placements_list const& placements = helper.get();
    BOOST_FOREACH(glyph_positions_ptr glyphs, placements)
    {
        if (glyphs->marker())
            render_marker(glyphs->marker_pos(),
                          *(glyphs->marker()->marker),
                          glyphs->marker()->transform,
                          sym.get_opacity(), sym.comp_op());
        ren.render(glyphs);
    }
}


template void agg_renderer<image_32>::process(shield_symbolizer const&,
                                              mapnik::feature_impl &,
                                              proj_transform const&);

}
