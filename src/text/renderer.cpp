/*****************************************************************************
 *
 * This file is part of Mapnik (c++ mapping toolkit)
 *
 * Copyright (C) 2013 Artem Pavlenko
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
#include <mapnik/text/renderer.hpp>
#include <mapnik/graphics.hpp>
#include <mapnik/grid/grid.hpp>
#include <mapnik/text/text_properties.hpp>
#include <mapnik/font_engine_freetype.hpp>
#include <mapnik/text/face.hpp>

// boost
#include <boost/make_shared.hpp>

// freetype2
extern "C"
{
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
}

namespace mapnik
{

struct glyph_t : mapnik::noncopyable
{
    FT_Glyph image;
    char_properties_ptr properties;

    glyph_t(FT_Glyph image_, char_properties_ptr properties_)
        : image(image_), properties(properties_) {}

    ~glyph_t () { FT_Done_Glyph(image);}

};

text_renderer::text_renderer (halo_rasterizer_e rasterizer, composite_mode_e comp_op, double scale_factor, stroker_ptr stroker)
    : rasterizer_(rasterizer),
      comp_op_(comp_op),
      scale_factor_(scale_factor),
      glyphs_(boost::make_shared<glyph_vector>()),
      stroker_(stroker)
{}


void text_renderer::prepare_glyphs(glyph_positions_ptr pos)
{
    //clear glyphs
    glyphs_->clear();

    FT_Matrix matrix;
    FT_Vector pen;
    FT_Error  error;

    glyph_positions::const_iterator itr = pos->begin(), end = pos->end();
    for (; itr != end; itr++)
    {
        glyph_info const& glyph = *(itr->glyph);
        glyph.face->set_character_sizes(glyph.format->text_size * scale_factor_); //TODO: Optimize this?

        matrix.xx = (FT_Fixed)( itr->rot.cos * 0x10000L);
        matrix.xy = (FT_Fixed)(-itr->rot.sin * 0x10000L);
        matrix.yx = (FT_Fixed)( itr->rot.sin * 0x10000L);
        matrix.yy = (FT_Fixed)( itr->rot.cos * 0x10000L);

        pixel_position pos = itr->pos + glyph.offset.rotate(itr->rot);
        pen.x = int(pos.x * 64);
        pen.y = int(pos.y * 64);

        FT_Face face = glyph.face->get_face();
        FT_Set_Transform(face, &matrix, &pen);

        error = FT_Load_Glyph(face, glyph.glyph_index, FT_LOAD_NO_HINTING);
        if (error) continue;

        FT_Glyph image;
        error = FT_Get_Glyph(face->glyph, &image);
        if (error) continue;

        // take ownership of the glyph
        glyphs_->push_back(new glyph_t(image, glyph.format));
    }
}

template <typename T>
void composite_bitmap(T & pixmap, FT_Bitmap *bitmap, unsigned rgba, int x, int y, double opacity, composite_mode_e comp_op)
{
    int x_max=x+bitmap->width;
    int y_max=y+bitmap->rows;
    int i,p,j,q;

    for (i=x,p=0;i<x_max;++i,++p)
    {
        for (j=y,q=0;j<y_max;++j,++q)
        {
            unsigned gray=bitmap->buffer[q*bitmap->width+p];
            if (gray)
            {
                pixmap.composite_pixel(comp_op, i, j, rgba, gray, opacity);
            }
        }
    }
}

template <typename T>
agg_text_renderer<T>::agg_text_renderer (pixmap_type & pixmap,
                                         halo_rasterizer_e rasterizer,
                                         composite_mode_e comp_op,
                                         double scale_factor,
                                         stroker_ptr stroker)
    : text_renderer(rasterizer, comp_op, scale_factor, stroker), pixmap_(pixmap)
{

}

template <typename T>
void agg_text_renderer<T>::render(glyph_positions_ptr pos)
{
    glyphs_->clear();
    prepare_glyphs(pos);
    FT_Error  error;
    FT_Vector start;
    unsigned height = pixmap_.height();
    pixel_position const& base_point = pos->get_base_point();

    start.x =  static_cast<FT_Pos>(base_point.x * (1 << 6));
    start.y =  static_cast<FT_Pos>((height - base_point.y) * (1 << 6));

    //render halo
    typename glyph_vector::iterator itr, end = glyphs_->end();
    double halo_radius = 0;
    char_properties_ptr format;
    for (itr = glyphs_->begin(); itr != end; ++itr)
    {
        if (itr->properties)
        {
            format = itr->properties;
            /* Settings have changed. */
            halo_radius = itr->properties->halo_radius * scale_factor_;
            //make sure we've got reasonable values.
            if (halo_radius <= 0.0 || halo_radius > 1024.0) break;
            stroker_->init(halo_radius);
        }
        FT_Glyph g;
        error = FT_Glyph_Copy(itr->image, &g);
        if (!error)
        {
            FT_Glyph_Transform(g,0,&start);
            if (rasterizer_ == HALO_RASTERIZER_FULL)
            {
                FT_Glyph_Stroke(&g, stroker_->get(), 1);
                error = FT_Glyph_To_Bitmap(&g, FT_RENDER_MODE_NORMAL, 0, 1);
                if (!error)
                {
                    FT_BitmapGlyph bit = (FT_BitmapGlyph)g;
                    composite_bitmap(pixmap_,
                                     &bit->bitmap,
                                     format->halo_fill.rgba(),
                                     bit->left,
                                     height - bit->top,
                                     format->text_opacity,
                                     comp_op_);
                }
            }
            else
            {
                error = FT_Glyph_To_Bitmap(&g, FT_RENDER_MODE_NORMAL, 0, 1);
                if (!error)
                {
                    FT_BitmapGlyph bit = (FT_BitmapGlyph)g;
                    render_halo(&bit->bitmap,
                                format->halo_fill.rgba(),
                                bit->left,
                                height - bit->top,
                                halo_radius,
                                format->text_opacity,
                                comp_op_);
                }
            }
        }
        FT_Done_Glyph(g);

    }

    //render actual text
    for (itr = glyphs_->begin(); itr != end; ++itr)
    {
        if (itr->properties)
        {
            format = itr->properties;
        }
        FT_Glyph_Transform(itr->image, 0, &start);

        error = FT_Glyph_To_Bitmap( &(itr->image),FT_RENDER_MODE_NORMAL,0,1);
        if (!error)
        {

            FT_BitmapGlyph bit = (FT_BitmapGlyph)itr->image;
            composite_bitmap(pixmap_, &bit->bitmap, format->fill.rgba(),
                             bit->left,
                             height - bit->top,
                             format->text_opacity,
                             comp_op_
                );
        }
    }
}


template <typename T>
void grid_text_renderer<T>::render(glyph_positions_ptr pos, value_integer feature_id)
{
    glyphs_->clear();
    prepare_glyphs(pos);
    FT_Error  error;
    FT_Vector start;
    unsigned height = pixmap_.height();
    pixel_position const& base_point = pos->get_base_point();
    start.x =  static_cast<FT_Pos>(base_point.x * (1 << 6));
    start.y =  static_cast<FT_Pos>((height - base_point.y) * (1 << 6));

    // now render transformed glyphs
    typename glyph_vector::iterator itr, end = glyphs_->end();
    double halo_radius = 0.;
    for (itr = glyphs_->begin(); itr != end; ++itr)
    {
        if (itr->properties)
        {
            halo_radius = itr->properties->halo_radius * scale_factor_;
        }
        FT_Glyph_Transform(itr->image, 0, &start);
        error = FT_Glyph_To_Bitmap(&(itr->image), FT_RENDER_MODE_NORMAL, 0, 1);
        if (!error)
        {

            FT_BitmapGlyph bit = (FT_BitmapGlyph)itr->image;
            render_halo_id(&bit->bitmap,
                           feature_id,
                           bit->left,
                           height - bit->top,
                           halo_radius);
        }
    }
}


template <typename T>
void agg_text_renderer<T>::render_halo(FT_Bitmap *bitmap,
                 unsigned rgba,
                 int x1,
                 int y1,
                 double halo_radius,
                 double opacity,
                 composite_mode_e comp_op)
{
    int width = bitmap->width;
    int height = bitmap->rows;
    int x, y;
    if (halo_radius < 1.0)
    {
        for (x=0; x < width; x++)
        {
            for (y=0; y < height; y++)
            {
                int gray = bitmap->buffer[y*bitmap->width+x];
                if (gray)
                {
                    pixmap_.composite_pixel(comp_op, x+x1-1, y+y1-1, rgba, gray*halo_radius*halo_radius, opacity);
                    pixmap_.composite_pixel(comp_op, x+x1,   y+y1-1, rgba, gray*halo_radius, opacity);
                    pixmap_.composite_pixel(comp_op, x+x1+1, y+y1-1, rgba, gray*halo_radius*halo_radius, opacity);

                    pixmap_.composite_pixel(comp_op, x+x1-1, y+y1,   rgba, gray*halo_radius, opacity);
                    pixmap_.composite_pixel(comp_op, x+x1,   y+y1,   rgba, gray, opacity);
                    pixmap_.composite_pixel(comp_op, x+x1+1, y+y1,   rgba, gray*halo_radius, opacity);

                    pixmap_.composite_pixel(comp_op, x+x1-1, y+y1+1, rgba, gray*halo_radius*halo_radius, opacity);
                    pixmap_.composite_pixel(comp_op, x+x1,   y+y1+1, rgba, gray*halo_radius, opacity);
                    pixmap_.composite_pixel(comp_op, x+x1+1, y+y1+1, rgba, gray*halo_radius*halo_radius, opacity);
                }
            }
        }
    } else {
        for (x=0; x < width; x++)
        {
            for (y=0; y < height; y++)
            {
                int gray = bitmap->buffer[y*bitmap->width+x];
                if (gray)
                {
                    for (int n=-halo_radius; n <=halo_radius; ++n)
                        for (int m=-halo_radius; m <= halo_radius; ++m)
                            pixmap_.composite_pixel(comp_op, x+x1+m, y+y1+n, rgba, gray, opacity);
                }
            }
        }
    }
}

template <typename T>
void grid_text_renderer<T>::render_halo_id(
                    FT_Bitmap *bitmap,
                    mapnik::value_integer feature_id,
                    int x1,
                    int y1,
                    int halo_radius)
{
    int width = bitmap->width;
    int height = bitmap->rows;
    int x, y;
    for (x=0; x < width; x++)
    {
        for (y=0; y < height; y++)
        {
            int gray = bitmap->buffer[y*bitmap->width+x];
            if (gray)
            {
                for (int n=-halo_radius; n <=halo_radius; ++n)
                    for (int m=-halo_radius; m <= halo_radius; ++m)
                        pixmap_.setPixel(x+x1+m,y+y1+n,feature_id);
            }
        }
    }
}

template <typename T>
grid_text_renderer<T>::grid_text_renderer(pixmap_type &pixmap,
                                          composite_mode_e comp_op,
                                          double scale_factor) :
    text_renderer(HALO_RASTERIZER_FAST, comp_op, scale_factor), pixmap_(pixmap)
{
}


template class agg_text_renderer<image_32>;
template class grid_text_renderer<grid>;
}
